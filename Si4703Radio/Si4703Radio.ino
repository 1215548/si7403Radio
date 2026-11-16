// Si4703Radio.ino -- ESP32-C3 Supermini + SSD1309 OLED + Si4703 FM 收音
//
// 界面:菜单系统(见 menu.h)。首页 KEY5 进菜单,菜单 KEY3 上/KEY4 下/
//   KEY5 确认/KEY1 返回。首页三态:收音播放(时间/频道/音量/RSSI/
//   台序号)/时钟(NTP 时间+日历+农历)/预留。
// 菜单:WiFi配置/收音机设置/任务状态/日期时间/关于版本/返回首页。
//   WiFi 页 KEY5 开关网页配置服务(AP "Si4703Radio" 192.168.4.1,
//   网页可改 SSID/密码/API Key,保存后自动重连;NTP 校时,农历走
//   api.530.news)。收音机设置页可自动搜台(预存台存 NVS)。
// 串口(115200,USB CDC):f 98.5 调频 / s 向上搜台 / S 向下搜台 /
//   v 12 音量 / m 静音开关 / i 读全部寄存器 / t 测试图 / r 重启收音机 /
//   p 列预存台 / ? 帮助
// 编译:见本目录 README.md(板子 ESP32C3 Dev Module,CDCOnBoot=cdc)。
#include <Arduino.h>
#include <WiFi.h>
#include "pins.h"
#include "led.h"
#include "keys.h"
#include "oled_display.h"
#include "radio_si4703.h"
#include "wifi_portal.h"
#include "presets.h"
#include "menu.h"

static Led g_led;
static Keys g_keys;
static OledDisplay g_oled;
static Si4703Radio g_radio;
static WifiPortal g_wifi;
static Presets g_presets;
static MenuUi g_menu;

static const uint8_t kKeyPins[KEY_COUNT] = {
    PIN_KEY1, PIN_KEY2, PIN_KEY3, PIN_KEY4, PIN_KEY5,
};

static void printRds() {
  const RdsInfo& r = g_radio.rds();
  if (!r.synced) {
    Serial.println(F("RDS no sync"));
    return;
  }
  Serial.printf("RDS PI=0x%04X PTY=%s TP=%d TA=%d groups=%lu bad=%lu\n", r.pi,
                RdsParser::ptyName(r.pty), (int)r.tp, (int)r.ta,
                (unsigned long)r.groups, (unsigned long)r.badGroups);
  if (r.psComplete) Serial.printf("  PS=\"%s\"\n", r.ps);
  if (r.rtLen) Serial.printf("  RT=\"%s\"\n", r.rt);
  if (r.hasCt)
    Serial.printf("  CT=%04d-%02d-%02d %02d:%02d\n", r.ctYear, r.ctMonth,
                  r.ctDay, r.ctHour, r.ctMinute);
}

static void printHelp() {
  Serial.println(F("---- serial commands (115200) ----"));
  Serial.println(F("f 98.5  tune   s seek up   S seek down"));
  Serial.println(F("v 12    volume 0-15   m mute toggle"));
  Serial.println(F("i       dump regs 00h-0Fh + RDS   t OLED test pattern"));
  Serial.println(F("r       re-init radio   p list presets   ? this help"));
  Serial.println(F("w       toggle WiFi config AP (Si4703Radio / 192.168.4.1)"));
  Serial.println(F("n       scan WiFi networks (temporarily turns AP off)"));
  Serial.println(F("keys HOME: K5 menu  K4/K3 ch  K2/K1 vol"));
  Serial.println(F("keys MENU: K3 up K4 down K5 enter K1 back"));
  Serial.println(F("RDS auto: PS/RT/CT on screen + serial when station sends RDS"));
}

void setup() {
  Serial.begin(115200);
  // USB CDC 默认发送超时 100ms、最多连续重试 20 次 ≈ 2 秒。PC 串口
  // 没打开/不读数据时,一次 Serial.printf 就能把主循环卡住 1-2 秒
  // (按键日志在按键处理前打印,表现为按键后 1-2 秒屏幕才动)。
  // 设为 0:缓冲区满就直接丢日志,绝不阻塞界面。
  Serial.setTxTimeoutMs(0);
  // 等 USB CDC 接上(最多 2 秒,不接 USB 也不卡死)
  uint32_t t0 = millis();
  while (!Serial && (uint32_t)(millis() - t0) < 2000) delay(10);
  Serial.println();
  Serial.println(F("Si4703Radio boot " FW_VERSION));
  Serial.printf("OLED CS=%d RES=%d DC=%d SCLK=%d MOSI=%d\n", PIN_OLED_CS,
                PIN_OLED_RES, PIN_OLED_DC, PIN_OLED_SCLK, PIN_OLED_MOSI);
  Serial.printf("SI4703 SEN=%d RST=%d SDIO=MOSI SCLK shared\n", PIN_SI4703_SEN,
                PIN_SI4703_RST);
  Serial.printf("LED=%d(LOW on) KEYS=%d,%d,%d,%d,%d(low=pressed)\n", PIN_LED,
                PIN_KEY1, PIN_KEY2, PIN_KEY3, PIN_KEY4, PIN_KEY5);

  g_led.begin(PIN_LED);
  g_led.on();  // 上电指示:初始化期间常亮
  g_keys.begin(kKeyPins);

  // ---- 开机动画:Linux 风启动日志 + 进度条(见 oled_display.cpp) ----
  g_oled.begin(FW_VERSION);
  Serial.println(F("OLED init done (write-only SPI, check screen lights)"));
  g_oled.bootLog("OLED SSD1309", 1);
  g_oled.bootProgress(10);
  g_oled.bootAnimateProgress(10, 30, 500);

  // ---- WiFi:读 NVS,自动连已保存的网络(非阻塞);农历任务随之启动 ----
  g_wifi.begin();
  g_oled.bootLog("WIFI ASYNC", 1);  // 非阻塞,连接结果稍后由主界面显示
  g_oled.bootProgress(40);
  // ---- 预存台:NVS 恢复 ----
  g_presets.begin();
  Serial.printf("presets loaded: %d\n", g_presets.count());
  char bl[24];
  snprintf(bl, sizeof(bl), "PRESETS %d", g_presets.count());
  g_oled.bootLog(bl, 1);
  g_oled.bootProgress(55);

  // ---- Si4703 ----
  g_oled.bootLog("SI4703 BUS", 0);  // 先显示进行中
  g_oled.bootProgress(65);
  bool radioOk = g_radio.begin();
  g_oled.bootUpdateLast(radioOk);
  if (radioOk) {
    Serial.printf("RADIO OK: %s DEVICEID=0x%04X CHIPID=0x%04X freq=%.2fMHz\n",
                  g_radio.isSi4703() ? "Si4703" : "Si4702(no RDS)",
                  g_radio.deviceId(), g_radio.chipId(), g_radio.freqMHz());
    g_led.set(g_radio.isStereo());
    if (g_presets.count() > 0) {
      // 有预存台:开机调到第 1 个预存台
      g_presets.tuneCurrent(g_radio);
      Serial.printf("tuned to preset 1/%d: %.2fMHz\n", g_presets.count(),
                    g_radio.freqMHz());
    }
    snprintf(bl, sizeof(bl), "TUNE %.2f", g_radio.freqMHz());
    g_oled.bootLog(bl, 1);
  } else {
    Serial.printf("RADIO FAIL: %s\n", g_radio.error());
    Serial.printf("  DEVICEID=0x%04X CHIPID=0x%04X\n", g_radio.deviceId(),
                  g_radio.chipId());
    Serial.println(F("  check: SEN/SDIO/SCLK/RST wiring, 3.3V power,"));
    Serial.println(F("  32.768kHz crystal on module, then send 'r' to retry."));
    g_led.blinkError();  // 慢闪=收音机故障
  }
  g_oled.bootProgress(90);
  g_menu.begin(&g_led, &g_keys, &g_oled, &g_radio, &g_wifi, &g_presets);
  g_oled.bootLog("SYSTEM READY", 1);
  g_oled.bootProgress(100);
  g_oled.bootFinish();
  // 开机自动搜台(无预存台时跑一次,见 menu.cpp bootScan)
  g_menu.bootScan();
  printHelp();
}

static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  char cmd = line.charAt(0);
  bool radioOk = g_radio.ok();
  if (cmd == '?') {
    printHelp();
  } else if (cmd == 'f') {
    float f = line.substring(1).toFloat();
    if (f >= RADIO_BAND_LOW_MHZ && f <= RADIO_BAND_HIGH_MHZ) {
      g_menu.manualTune(f);
      Serial.printf("tune %.2fMHz RSSI=%d\n", g_radio.freqMHz(),
                    g_radio.rssi());
    } else {
      Serial.println(F("freq out of 87.5-108MHz"));
    }
  } else if (cmd == 's' || cmd == 'S') {
    if (radioOk) {
      bool found = g_radio.seek(cmd == 's');
      Serial.printf("SEEK %s: %.2fMHz RSSI=%d\n", found ? "found" : "fail",
                    g_radio.freqMHz(), g_radio.rssi());
      char buf[24];
      snprintf(buf, sizeof(buf), found ? "SEEK %.2fMHz" : "SEEK FAIL %.2fMHz",
               g_radio.freqMHz());
      g_menu.showMsg(buf);
    } else {
      Serial.println(F("no radio"));
    }
  } else if (cmd == 'v') {
    int v = line.substring(1).toInt();
    if (v >= 0 && v <= 15) {
      if (radioOk) g_radio.setVolume((uint8_t)v);
      Serial.printf("VOL %d\n", v);
      char buf[16];
      snprintf(buf, sizeof(buf), "VOL %d", v);
      g_menu.showMsg(buf);
    }
  } else if (cmd == 'm') {
    if (radioOk) {
      bool m = !g_radio.isMuted();
      g_radio.setMute(m);
      Serial.printf("MUTE %s\n", m ? "ON" : "OFF");
      g_menu.showMsg(m ? "MUTE ON" : "MUTE OFF");
    } else {
      Serial.println(F("no radio"));
    }
  } else if (cmd == 'i') {
    Serial.printf("DEVICEID=0x%04X CHIPID=0x%04X %s ok=%d\n", g_radio.deviceId(),
                  g_radio.chipId(), g_radio.isSi4703() ? "Si4703" : "Si4702",
                  (int)radioOk);
    uint16_t regs[16];
    g_radio.readAllRegs(regs);
    for (uint8_t i = 0; i < 16; i++) {
      Serial.printf("  %02Xh=0x%04X\n", i, regs[i]);
    }
    Serial.printf("freq=%.2fMHz RSSI=%d ST=%d VOL=%d MUTE=%d\n",
                  g_radio.freqMHz(), g_radio.rssi(), (int)g_radio.isStereo(),
                  g_radio.volume(), (int)g_radio.isMuted());
    Serial.printf("presets: %d (current %d)\n", g_presets.count(),
                  g_presets.current() + 1);
    Serial.printf("wifi: %s ssid=%s ip=%s portal=%s\n",
                  g_wifi.connected() ? "connected" : "off",
                  g_wifi.ssid().c_str(), g_wifi.ip().c_str(),
                  g_wifi.portalOn() ? "ON" : "OFF");
    printRds();
  } else if (cmd == 't') {
    g_oled.showTestPattern();
    Serial.println(F("OLED test pattern shown"));
    delay(1500);
    g_menu.refresh();
  } else if (cmd == 'r') {
    Serial.println(F("re-init radio..."));
    g_oled.showMessage("RADIO INIT...", "");
    bool ok = g_radio.begin();
    g_menu.setRadioOk(ok);
    if (ok) {
      Serial.printf("RADIO OK: %s CHIPID=0x%04X freq=%.2fMHz\n",
                    g_radio.isSi4703() ? "Si4703" : "Si4702(no RDS)",
                    g_radio.chipId(), g_radio.freqMHz());
      g_menu.showMsg("RADIO OK");
      g_led.set(g_radio.isStereo());
    } else {
      Serial.printf("RADIO FAIL: %s\n", g_radio.error());
      g_menu.showMsg("RADIO FAIL");
      g_led.blinkError();
    }
  } else if (cmd == 'w') {
    g_wifi.setPortal(!g_wifi.portalOn());
    Serial.printf("PORTAL %s apIP=%s apSSID=%s apMAC=%s mode=%d tx=%d\n",
                  g_wifi.portalOn() ? "ON" : "OFF", g_wifi.apIP().c_str(),
                  WiFi.softAPSSID().c_str(),
                  WiFi.softAPmacAddress().c_str(), (int)WiFi.getMode(),
                  (int)WiFi.getTxPower());
    g_menu.refresh();
  } else if (cmd == 'n') {
    // 诊断:先关 AP,再用 STA 扫一圈,最后恢复原 AP 状态
    bool wasOn = g_wifi.portalOn();
    if (wasOn) g_wifi.setPortal(false);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false);
    Serial.printf("WIFI scan: %d networks\n", n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %d: \"%s\" rssi=%d ch=%d\n", i + 1,
                    WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    }
    WiFi.scanDelete();
    if (wasOn) g_wifi.setPortal(true);
    g_menu.refresh();
  } else if (cmd == 'p') {
    Serial.printf("presets: %d\n", g_presets.count());
    for (int i = 0; i < g_presets.count(); i++) {
      Station s = g_presets.get(i);
      Serial.printf("  %d: %.2fMHz RSSI %d%s\n", i + 1, s.freq, s.rssi,
                    i == g_presets.current() ? " <=" : "");
    }
  } else {
    Serial.println(F("unknown cmd, send ? for help"));
  }
}

void loop() {
  g_menu.tick();
  handleSerial();
}
