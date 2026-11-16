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

String WifiPortal::scanSSID(int i) const {
  if (i < 0 || i >= _scanCount) return "";
  return _scanSSIDs[i];
}

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
    // 开 AP + 扫网 + 起 WebServer
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, "");
    _scanCount = 0;
    int n = WiFi.scanNetworks(false);
    for (int i = 0; i < n && _scanCount < 16; i++) {
      String s = WiFi.SSID(i);
      if (s.length() == 0) continue;
      bool dup = false;
      for (int j = 0; j < _scanCount; j++)
        if (_scanSSIDs[j] == s) dup = true;
      if (!dup) _scanSSIDs[_scanCount++] = s;
    }
    s_nvs.begin("wifi", false);
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
           "SSID: <input name='ssid' list='nets' value='" +
           htmlEscape(savedSsid) + "'><datalist id='nets'>";
      for (int i = 0; i < _scanCount; i++)
        h += "<option value='" + htmlEscape(_scanSSIDs[i]) + "'>";
      h += "</datalist><br>密码: <input type='password' name='pass'><br>"
           "API Key(预留): <input name='apikey' value='" +
           htmlEscape(savedKey) +
           "'><br><input type='submit' value='保存并连接'></form>"
           "<p>保存后设备自动连接,约 10 秒后刷新本页查看状态。"
           "API Key 仅保存,暂无功能。</p></body></html>";
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
    s_server.begin();
    _portalOn = true;
    Serial.println("[PORTAL] on 192.168.4.1");
  } else {
    s_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);  // 保持 STA(若已连接则不断)
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
