// wifi_portal.cpp -- 见 wifi_portal.h
#include "wifi_portal.h"
#include "pins.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

static WebServer s_server(80);
static Preferences s_nvs;
static bool s_routesReady = false;  // 路由只注册一次,反复开关不重复挂

static const char* kWeekDays[] = {"星期日", "星期一", "星期二", "星期三",
                                  "星期四", "星期五", "星期六"};

void WifiPortal::begin() {
  _lunarMutex = xSemaphoreCreateMutex();
  // 农历任务:独立 12KB 栈,见头文件说明
  xTaskCreate(lunarTask, "lunarFetch", 12288, this, 1, &_lunarTask);
  s_nvs.begin("wifi", false);
  String ssid = s_nvs.getString("ssid", "");
  String pass = s_nvs.getString("pass", "");
  WiFi.mode(WIFI_STA);
  // ESP32-C3 Supermini 的射频前端在高功率下会饱和:softAP 返回成功、
  // IP 也正常,但手机扫不到热点;STA 也可能连不上。降到 8.5dBm 后
  // 正常(arduino-esp32 issue #6551 多例 C3 Supermini 同款)。
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  if (ssid.length() > 0) {
    _ssidCache = ssid;
    WiFi.begin(ssid.c_str(), pass.c_str());  // 非阻塞,成败在 tick 里看
    Serial.printf("[WIFI] connecting to %s ...\n", ssid.c_str());
  }
}

bool WifiPortal::connected() const { return _staConnected; }

String WifiPortal::ssid() const {
  return _staConnected ? WiFi.SSID() : _ssidCache;
}

String WifiPortal::ip() const {
  return _staConnected ? WiFi.localIP().toString() : String("-");
}

int WifiPortal::rssi() const {
  return _staConnected ? WiFi.RSSI() : 0;
}

bool WifiPortal::timeValid() const { return _timeOk; }

bool WifiPortal::getTime(struct tm& out) const {
  if (!_timeOk) return false;
  return getLocalTime(&out, 100);
}

const char* WifiPortal::weekDayName() const {
  struct tm t;
  if (!getTime(t)) return "";
  return kWeekDays[t.tm_wday % 7];
}

String WifiPortal::lunarFull() const { return _lunarFull; }
String WifiPortal::lunarShort() const { return _lunarShort; }
String WifiPortal::lunarMonthDay() const { return _lunarShort; }

void WifiPortal::onConnected() {
  Serial.printf("[WIFI] connected %s %s RSSI %d\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (!_ntpStarted) {
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org",
               "time.google.com");
    _ntpStarted = true;
  }
  requestLunar();
}

void WifiPortal::tick() {
  if (_portalOn) s_server.handleClient();
  // STA 状态机(非阻塞)
  bool nowConn = (WiFi.status() == WL_CONNECTED);
  if (nowConn && !_staConnected) {
    _staConnected = true;
    onConnected();
  } else if (!nowConn && _staConnected) {
    _staConnected = false;
    _timeOk = false;
    Serial.println("[WIFI] disconnected");
  }
  // NTP 状态:每 2s 查一次,年 >= 2020 算有效
  uint32_t nowMs = millis();
  if (_staConnected && nowMs - _lastNtpCheckMs >= 2000) {
    _lastNtpCheckMs = nowMs;
    struct tm t;
    if (getLocalTime(&t, 100) && t.tm_year >= 120) {
      if (!_timeOk) {
        _timeOk = true;
        Serial.printf("[NTP] %04d-%02d-%02d %02d:%02d:%02d\n",
                      t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
                      t.tm_min, t.tm_sec);
      }
    }
  }
  // 农历:每 5s 检查是否需要拉取(节流在 requestLunar 里)
  static uint32_t lastLunarCheck = 0;
  if (nowMs - lastLunarCheck >= 5000) {
    lastLunarCheck = nowMs;
    mergeLunar();
    if (_staConnected && _timeOk) requestLunar();
  }
}

// ---------- 网页配置服务 ----------

static String htmlEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

void WifiPortal::setPortal(bool on) {
  if (on == _portalOn) return;
  if (on) {
    // 只起 AP,不再做阻塞式 scanNetworks:扫描会占用射频、打断刚起
    // 的 AP,实机表现为界面 AP ON 但手机搜不到热点。SSID 由用户在
    // 网页里手输(输入框会带出上次保存的值)。
    WiFi.mode(WIFI_AP_STA);
    // 见 begin():C3 Supermini 必须降低发射功率,否则 AP 不可见
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    bool apOk = WiFi.softAP(AP_SSID, "");
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // softAP 后再设一次,防被复位
    Serial.printf("[PORTAL] softAP=%d ssid=%s ip=%s ch=%d tx=%d\n",
                  (int)apOk, AP_SSID, WiFi.softAPIP().toString().c_str(),
                  WiFi.channel(), (int)WiFi.getTxPower());
    if (!apOk || (uint32_t)WiFi.softAPIP() == 0) {
      Serial.println("[PORTAL] AP failed, retry after mode reset");
      WiFi.mode(WIFI_AP_STA);
      delay(200);
      apOk = WiFi.softAP(AP_SSID, "");
      Serial.printf("[PORTAL] retry softAP=%d ip=%s\n", (int)apOk,
                    WiFi.softAPIP().toString().c_str());
    }
    if (!apOk || (uint32_t)WiFi.softAPIP() == 0) {
      // 不要假装成功:UI 继续显示 AP OFF,串口给出原因
      Serial.println("[PORTAL] AP start failed");
      WiFi.mode(WIFI_STA);
      return;
    }
    s_nvs.begin("wifi", false);
    if (!s_routesReady) {
    s_server.on("/", [this]() {
      // SSID/API Key 每次打开页面时从 NVS 现读(保存后立即生效)
      String savedSsid = s_nvs.getString("ssid", "");
      String savedKey = s_nvs.getString("apikey", "");
      String h = "<html><head><meta charset='utf-8'>"
                 "<meta name='viewport' content='width=device-width'>"
                 "<title>Si4703Radio</title></head><body>"
                 "<h2>Si4703Radio WiFi 配置</h2><p>状态: ";
      h += _staConnected ? "已连接 " + htmlEscape(WiFi.SSID()) + " " +
                               WiFi.localIP().toString()
                         : "未连接";
      h += "</p><form action='/save' method='POST'>"
           "WiFi 名称: <input name='ssid' placeholder='输入路由器 WiFi 名称' value='" +
           htmlEscape(savedSsid) +
           "'><br>密码: <input type='password' name='pass'><br>"
           "API Key(预留): <input name='apikey' value='" +
           htmlEscape(savedKey) +
           "'><br><input type='submit' value='保存并连接'></form>"
           "<p>手机保持连接 Si4703Radio 热点,保存后设备自动连接路由器,"
           "约 10 秒后刷新本页查看状态。API Key 仅保存,暂无功能。</p>"
           "</body></html>";
      s_server.send(200, "text/html; charset=utf-8", h);
    });
    s_server.on("/save", [this]() {
      String ssid = s_server.arg("ssid");
      String pass = s_server.arg("pass");
      String key = s_server.arg("apikey");
      s_nvs.begin("wifi", false);
      s_nvs.putString("ssid", ssid);
      s_nvs.putString("pass", pass);
      s_nvs.putString("apikey", key);
      _ssidCache = ssid;
      _staConnected = false;
      _timeOk = false;
      WiFi.setTxPower(WIFI_POWER_8_5dBm);  // C3 Supermini 高功率会连不上
      WiFi.begin(ssid.c_str(), pass.c_str());
      Serial.printf("[WIFI] portal saved %s, reconnecting...\n",
                    ssid.c_str());
      s_server.send(200, "text/html; charset=utf-8",
                    "<html><head><meta charset='utf-8'>"
                    "<meta http-equiv='refresh' content='12;url=/'>"
                    "</head><body>已保存,正在连接 "
                        + htmlEscape(ssid) +
                        ",12 秒后自动返回状态页。</body></html>");
    });
    s_server.onNotFound(
        []() { s_server.send(404, "text/plain", "404 Not Found"); });
      s_routesReady = true;
    }
    s_server.begin();
    _portalOn = true;
    Serial.printf("[PORTAL] on 192.168.4.1 mode=%d stations=%d\n",
                  (int)WiFi.getMode(), (int)WiFi.softAPgetStationNum());
  } else {
    s_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);  // 保持 STA(若已连接则不断)
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    _portalOn = false;
    Serial.println("[PORTAL] off");
  }
}

// ---------- 农历(独立任务) ----------

bool WifiPortal::jsonStringField(const String& payload, const char* key,
                                 String& value) {
  String pat = String("\"") + key + "\"";
  int i = payload.indexOf(pat);
  if (i < 0) return false;
  int c = payload.indexOf(':', i + pat.length());
  if (c < 0) return false;
  int s = payload.indexOf('"', c);
  if (s < 0) return false;
  int e = payload.indexOf('"', s + 1);
  if (e <= s) return false;
  value = payload.substring(s + 1, e);
  return value.length() > 0;
}

bool WifiPortal::fetchLunar(String& ganzhi, String& zodiac, String& monthName,
                            String& dayName) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();  // 公开数据、无凭证,免在 ESP32 上维护 CA 表
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(client, "https://api.530.news/api/lunar?date=today&timezone=8");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  int ci = payload.indexOf("\"code\"");
  if (ci < 0) return false;
  int cc = payload.indexOf(':', ci + 6);
  if (cc < 0) return false;
  int cs = cc + 1;
  while (cs < (int)payload.length() && payload[cs] == ' ') cs++;
  if (payload.substring(cs, cs + 3) != "200") return false;
  if (!jsonStringField(payload, "yearInGanZhi", ganzhi)) return false;
  if (!jsonStringField(payload, "zodiac", zodiac)) return false;
  if (!jsonStringField(payload, "lunarMonthName", monthName)) return false;
  if (!jsonStringField(payload, "lunarDayName", dayName)) return false;
  return true;
}

void WifiPortal::lunarTask(void* arg) {
  WifiPortal* self = (WifiPortal*)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    String gz, zod, mn, dn;
    if (self->fetchLunar(gz, zod, mn, dn)) {
      time_t nowSec = time(nullptr);
      struct tm tmNow;
      localtime_r(&nowSec, &tmNow);
      if (xSemaphoreTake(self->_lunarMutex, portMAX_DELAY) == pdTRUE) {
        snprintf(self->_lunarResFull, sizeof(self->_lunarResFull), "%s%s年%s%s",
                 gz.c_str(), zod.c_str(), mn.c_str(), dn.c_str());
        snprintf(self->_lunarResShort, sizeof(self->_lunarResShort), "%s%s",
                 mn.c_str(), dn.c_str());
        snprintf(self->_lunarResDate, sizeof(self->_lunarResDate), "%d-%d-%d",
                 tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
        self->_lunarReady = true;
        xSemaphoreGive(self->_lunarMutex);
      }
    }
    self->_lunarFetching = false;
  }
}

void WifiPortal::requestLunar() {
  if (!_lunarTask || _lunarFetching) return;
  // 已有当天结果就不再拉
  struct tm t;
  if (_lunarOk && getTime(t)) {
    char today[16];
    snprintf(today, sizeof(today), "%d-%d-%d", t.tm_year + 1900, t.tm_mon + 1,
             t.tm_mday);
    if (String(today) == String(_lunarResDate)) return;
  }
  uint32_t nowMs = millis();
  if (_lastLunarTryMs != 0 && nowMs - _lastLunarTryMs < 60000) return;
  _lastLunarTryMs = nowMs;
  _lunarFetching = true;
  xTaskNotifyGive(_lunarTask);
}

void WifiPortal::mergeLunar() {
  if (!_lunarMutex) return;
  if (xSemaphoreTake(_lunarMutex, 0) == pdTRUE) {
    if (_lunarReady) {
      _lunarFull = String(_lunarResFull);
      _lunarShort = String(_lunarResShort);
      _lunarOk = true;
      _lunarReady = false;
      Serial.printf("[LUNAR] %s\n", _lunarFull.c_str());
    }
    xSemaphoreGive(_lunarMutex);
  }
}
