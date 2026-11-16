// Si4703Radio.ino -- ESP32-C3 Supermini + SSD1309 OLED + Si4703 FM 收音
//
// 分阶段调通,先 1 后 2:
//   阶段 1(OLED/按键/LED):上电即见启动画面,按任意键串口+屏幕都有反馈,
//     LED 闪一下;串口发 t 看测试图。收音机还没通也不影响这一阶段。
//   阶段 2(Si4703):上电自动读 ID、上电、调到默认频率;成功则主界面
//     显示频率/RSSI/立体声/音量,LED 常亮=立体声信号;失败则屏幕显示
//     NO RADIO、LED 慢闪、串口打印诊断,按键/屏幕照常工作,串口发 r 重试。
//
// 按键(按下=低电平):KEY1 音量- / KEY2 音量+ / KEY3 频率-0.1MHz /
//   KEY4 频率+0.1MHz / KEY5 向上搜台。静音走串口 m。
// 串口(115200,USB CDC):f 98.5 调频 / s 向上搜台 / S 向下搜台 /
//   v 12 音量 / m 静音开关 / i 读全部寄存器 / t 测试图 / r 重启收音机 / ? 帮助
// 编译:见本目录 README.md(板子 ESP32C3 Dev Module,CDCOnBoot=cdc)。
#include <Arduino.h>
#include "pins.h"
#include "led.h"
#include "keys.h"
#include "oled_display.h"
#include "radio_si4703.h"

static Led g_led;
static Keys g_keys;
static OledDisplay g_oled;
static Si4703Radio g_radio;

static const uint8_t kKeyPins[KEY_COUNT] = {
    PIN_KEY1, PIN_KEY2, PIN_KEY3, PIN_KEY4, PIN_KEY5,
};

static bool g_radioOk = false;
static float g_freqMHz = RADIO_DEFAULT_FREQ_MHZ;
static int g_volume = RADIO_DEFAULT_VOLUME;
static bool g_muted = false;
static char g_msg[32] = "";
static bool g_uiDirty = true;
static uint32_t g_lastStatusMs = 0;

static void printHelp() {
  Serial.println(F("---- serial commands (115200) ----"));
  Serial.println(F("f 98.5  tune   s seek up   S seek down"));
  Serial.println(F("v 12    volume 0-15   m mute toggle"));
  Serial.println(F("i       dump regs 00h-0Fh   t OLED test pattern"));
  Serial.println(F("r       re-init radio   ? this help"));
  Serial.println(F("keys: K1 VOL- K2 VOL+ K3 F- K4 F+ K5 SEEK"));
}

static void refreshUi() {
  if (g_radioOk) {
    g_freqMHz = g_radio.freqMHz();
    g_volume = g_radio.volume();
    g_muted = g_radio.isMuted();
  }
  g_oled.showRadio(g_freqMHz, g_radioOk ? g_radio.rssi() : 0,
                   g_radioOk ? g_radio.isStereo() : false, g_volume, g_muted,
                   g_radioOk, g_msg);
}

static void setMsg(const char* fmt, float v) {
  snprintf(g_msg, sizeof(g_msg), fmt, v);
  g_uiDirty = true;
}

void setup() {
  Serial.begin(115200);
  // 等 USB CDC 接上(最多 2 秒,不接 USB 也不卡死)
  uint32_t t0 = millis();
  while (!Serial && (uint32_t)(millis() - t0) < 2000) delay(10);
  Serial.println();
  Serial.println(F("Si4703Radio boot"));
  Serial.printf("OLED CS=%d RES=%d DC=%d SCLK=%d MOSI=%d\n", PIN_OLED_CS,
                PIN_OLED_RES, PIN_OLED_DC, PIN_OLED_SCLK, PIN_OLED_MOSI);
  Serial.printf("SI4703 SEN=%d RST=%d SDIO=MOSI SCLK shared\n", PIN_SI4703_SEN,
                PIN_SI4703_RST);
  Serial.printf("LED=%d(LOW on) KEYS=%d,%d,%d,%d,%d(low=pressed)\n", PIN_LED,
                PIN_KEY1, PIN_KEY2, PIN_KEY3, PIN_KEY4, PIN_KEY5);

  g_led.begin(PIN_LED);
  g_led.on();  // 上电指示:初始化期间常亮
  g_keys.begin(kKeyPins);
  g_oled.begin();  // 显示 "Si4703 FM / OLED OK"
  Serial.println(F("OLED init done (write-only SPI, check screen lights)"));
  delay(800);

  // ---- 阶段 2:Si4703 ----
  g_radioOk = g_radio.begin();
  if (g_radioOk) {
    Serial.printf("RADIO OK: %s DEVICEID=0x%04X CHIPID=0x%04X freq=%.2fMHz\n",
                  g_radio.isSi4703() ? "Si4703" : "Si4702(no RDS)",
                  g_radio.deviceId(), g_radio.chipId(), g_radio.freqMHz());
    snprintf(g_msg, sizeof(g_msg), "RADIO OK %.2fMHz", g_radio.freqMHz());
    g_led.set(g_radio.isStereo());
  } else {
    Serial.printf("RADIO FAIL: %s\n", g_radio.error());
    Serial.printf("  DEVICEID=0x%04X CHIPID=0x%04X\n", g_radio.deviceId(),
                  g_radio.chipId());
    Serial.println(F("  check: SEN/SDIO/SCLK/RST wiring, 3.3V power,"));
    Serial.println(F("  32.768kHz crystal on module, then send 'r' to retry."));
    snprintf(g_msg, sizeof(g_msg), "RADIO FAIL");
    g_led.blinkError();  // 慢闪=收音机故障
  }
  printHelp();
  refreshUi();
  g_uiDirty = false;
}

static void handleKeyPress(uint8_t pressed) {
  // 每按一个键 LED 闪一下(按键反馈),串口+屏幕同步显示
  g_led.blink(120);
  if (pressed & KEY1_MASK) {
    if (g_volume > 0) g_volume--;
    if (g_radioOk) g_radio.setVolume((uint8_t)g_volume);
    Serial.printf("KEY1 VOL- -> %d\n", g_volume);
    snprintf(g_msg, sizeof(g_msg), "KEY1 VOL- %d", g_volume);
  }
  if (pressed & KEY2_MASK) {
    if (g_volume < 15) g_volume++;
    if (g_radioOk) g_radio.setVolume((uint8_t)g_volume);
    Serial.printf("KEY2 VOL+ -> %d\n", g_volume);
    snprintf(g_msg, sizeof(g_msg), "KEY2 VOL+ %d", g_volume);
  }
  if (pressed & KEY3_MASK) {
    g_freqMHz -= RADIO_SPACE_MHZ;
    if (g_freqMHz < RADIO_BAND_LOW_MHZ) g_freqMHz = RADIO_BAND_LOW_MHZ;
    if (g_radioOk) {
      bool ok = g_radio.tune(g_freqMHz);
      g_freqMHz = g_radio.freqMHz();
      Serial.printf("KEY3 F- tune %.2fMHz %s RSSI=%d %s\n", g_freqMHz,
                    ok ? "OK" : "no STC", g_radio.rssi(),
                    g_radio.lastAfcRail() ? "AFC-RAIL" : "");
    } else {
      Serial.printf("KEY3 F- -> %.2fMHz (no radio)\n", g_freqMHz);
    }
    setMsg("KEY3 %.2fMHz", g_freqMHz);
  }
  if (pressed & KEY4_MASK) {
    g_freqMHz += RADIO_SPACE_MHZ;
    if (g_freqMHz > RADIO_BAND_HIGH_MHZ) g_freqMHz = RADIO_BAND_HIGH_MHZ;
    if (g_radioOk) {
      bool ok = g_radio.tune(g_freqMHz);
      g_freqMHz = g_radio.freqMHz();
      Serial.printf("KEY4 F+ tune %.2fMHz %s RSSI=%d %s\n", g_freqMHz,
                    ok ? "OK" : "no STC", g_radio.rssi(),
                    g_radio.lastAfcRail() ? "AFC-RAIL" : "");
    } else {
      Serial.printf("KEY4 F+ -> %.2fMHz (no radio)\n", g_freqMHz);
    }
    setMsg("KEY4 %.2fMHz", g_freqMHz);
  }
  if (pressed & KEY5_MASK) {
    if (g_radioOk) {
      g_oled.showMessage("SEEKING...", "up");
      Serial.println(F("KEY5 SEEK up..."));
      bool found = g_radio.seek(true);
      g_freqMHz = g_radio.freqMHz();
      Serial.printf("SEEK %s: %.2fMHz RSSI=%d %s\n",
                    found ? "found" : "fail/edge", g_freqMHz, g_radio.rssi(),
                    g_radio.lastSeekFailed() ? "SF/BL" : "");
      setMsg(found ? "SEEK %.2fMHz" : "SEEK FAIL %.2fMHz", g_freqMHz);
    } else {
      Serial.println(F("KEY5 SEEK (no radio)"));
      snprintf(g_msg, sizeof(g_msg), "KEY5 NO RADIO");
    }
  }
  g_uiDirty = true;
}

static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  char cmd = line.charAt(0);
  if (cmd == '?') {
    printHelp();
  } else if (cmd == 'f') {
    float f = line.substring(1).toFloat();
    if (f >= RADIO_BAND_LOW_MHZ && f <= RADIO_BAND_HIGH_MHZ) {
      g_freqMHz = f;
      if (g_radioOk) {
        bool ok = g_radio.tune(g_freqMHz);
        g_freqMHz = g_radio.freqMHz();
        Serial.printf("tune %.2fMHz %s RSSI=%d\n", g_freqMHz,
                      ok ? "OK" : "no STC", g_radio.rssi());
      }
      setMsg("TUNE %.2fMHz", g_freqMHz);
    } else {
      Serial.println(F("freq out of 87.5-108MHz"));
    }
  } else if (cmd == 's' || cmd == 'S') {
    if (g_radioOk) {
      g_oled.showMessage("SEEKING...", cmd == 's' ? "up" : "down");
      bool found = g_radio.seek(cmd == 's');
      g_freqMHz = g_radio.freqMHz();
      Serial.printf("SEEK %s: %.2fMHz RSSI=%d\n", found ? "found" : "fail",
                    g_freqMHz, g_radio.rssi());
      setMsg(found ? "SEEK %.2fMHz" : "SEEK FAIL %.2fMHz", g_freqMHz);
    } else {
      Serial.println(F("no radio"));
    }
  } else if (cmd == 'v') {
    int v = line.substring(1).toInt();
    if (v >= 0 && v <= 15) {
      g_volume = v;
      if (g_radioOk) g_radio.setVolume((uint8_t)v);
      Serial.printf("VOL %d\n", g_volume);
      snprintf(g_msg, sizeof(g_msg), "VOL %d", g_volume);
      g_uiDirty = true;
    }
  } else if (cmd == 'm') {
    if (g_radioOk) {
      g_muted = !g_muted;
      g_radio.setMute(g_muted);
      Serial.printf("MUTE %s\n", g_muted ? "ON" : "OFF");
      snprintf(g_msg, sizeof(g_msg), "%s", g_muted ? "MUTE ON" : "MUTE OFF");
      g_uiDirty = true;
    } else {
      Serial.println(F("no radio"));
    }
  } else if (cmd == 'i') {
    Serial.printf("DEVICEID=0x%04X CHIPID=0x%04X %s ok=%d\n", g_radio.deviceId(),
                  g_radio.chipId(), g_radio.isSi4703() ? "Si4703" : "Si4702",
                  (int)g_radioOk);
    uint16_t regs[16];
    g_radio.readAllRegs(regs);
    for (uint8_t i = 0; i < 16; i++) {
      Serial.printf("  %02Xh=0x%04X\n", i, regs[i]);
    }
    Serial.printf("freq=%.2fMHz RSSI=%d ST=%d VOL=%d MUTE=%d\n", g_freqMHz,
                  g_radio.rssi(), (int)g_radio.isStereo(), g_volume,
                  (int)g_muted);
    g_uiDirty = true;
  } else if (cmd == 't') {
    g_oled.showTestPattern();
    Serial.println(F("OLED test pattern shown"));
    delay(1500);
    g_uiDirty = true;
  } else if (cmd == 'r') {
    Serial.println(F("re-init radio..."));
    g_oled.showMessage("RADIO INIT...", "");
    g_radioOk = g_radio.begin();
    if (g_radioOk) {
      Serial.printf("RADIO OK: %s CHIPID=0x%04X freq=%.2fMHz\n",
                    g_radio.isSi4703() ? "Si4703" : "Si4702(no RDS)",
                    g_radio.chipId(), g_radio.freqMHz());
      snprintf(g_msg, sizeof(g_msg), "RADIO OK %.2fMHz", g_radio.freqMHz());
      g_led.set(g_radio.isStereo());
    } else {
      Serial.printf("RADIO FAIL: %s\n", g_radio.error());
      snprintf(g_msg, sizeof(g_msg), "RADIO FAIL");
      g_led.blinkError();
    }
    g_uiDirty = true;
  } else {
    Serial.println(F("unknown cmd, send ? for help"));
  }
}

void loop() {
  g_led.update();
  uint8_t pressed = g_keys.update();
  if (pressed) handleKeyPress(pressed);
  handleSerial();

  // 每秒读一次状态:刷新 RSSI/立体声/实际频率,LED 跟随立体声
  if (g_radioOk && (uint32_t)(millis() - g_lastStatusMs) >= 1000) {
    g_lastStatusMs = millis();
    if (g_radio.readStatus()) {
      g_led.set(g_radio.isStereo() && !g_radio.isMuted());
      g_uiDirty = true;
    }
  }
  if (g_uiDirty) {
    refreshUi();
    g_uiDirty = false;
  }
}
