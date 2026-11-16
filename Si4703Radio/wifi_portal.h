// wifi_portal.h -- WiFi 配网 + 网页配置服务 + NTP + 农历
// 责任:
//   - NVS("wifi")保存 SSID/密码/API Key;上电自动连已保存的 WiFi(非阻塞,
//     连上后自动启动 NTP,失败不阻塞主循环)。
//   - 网页配置服务(开关由菜单控制):AP "Si4703Radio"(192.168.4.1)+
//     WebServer 80 端口;网页可改 SSID/密码/API Key(预留),保存后自动重连。
//   - NTP(阿里云/pool):连上后校时;农历走 S2 工程同款接口
//     https://api.530.news/api/lunar(HTTPS,公开数据,setInsecure)。
//   - TLS 握手吃栈:农历拉取放在独立 12KB 任务里(S2 工程同款教训:
//     loop 任务默认栈 8KB,直接跑 WiFiClientSecure 易栈溢出重启)。
#pragma once
#include <Arduino.h>

class WifiPortal {
public:
  void begin();  // 读 NVS,启动 STA 连接(非阻塞);建农历任务
  void tick();   // WebServer 处理 + 连接状态机 + NTP 检查 + 农历结果合并
  // 状态查询
  bool connected() const;
  String ssid() const;
  String ip() const;
  int rssi() const;
  // 网页配置服务开关
  bool portalOn() const { return _portalOn; }
  void setPortal(bool on);
  String apIP() const { return "192.168.4.1"; }
  // 时间
  bool timeValid() const;
  bool getTime(struct tm& out) const;
  const char* weekDayName() const;  // "星期一" 之类(需 timeValid)
  // 农历(取自 API,失败保留旧值)
  bool lunarValid() const { return _lunarOk; }
  String lunarFull() const;   // "丙午马年十月初八"
  String lunarShort() const;  // "十月初八"
  String lunarMonthDay() const;  // "十月初八"(同 lunarShort)
private:
  void onConnected();      // 刚连上:启动 NTP + 请求农历
  void requestLunar();     // 通知农历任务拉取(60s 节流)
  void mergeLunar();       // 把任务结果并入主循环
  static void lunarTask(void* arg);
  bool fetchLunar(String& ganzhi, String& zodiac, String& monthName,
                  String& dayName);
  static bool jsonStringField(const String& payload, const char* key,
                              String& value);

  bool _portalOn = false;
  bool _staConnected = false;
  bool _ntpStarted = false;
  bool _timeOk = false;
  bool _lunarOk = false;
  String _lunarFull;    // "丙午马年十月初八"
  String _lunarShort;   // "十月初八"
  String _ssidCache;    // 当前连接的 SSID(连接中为空)
  uint32_t _lastNtpCheckMs = 0;
  uint32_t _lastServerMs = 0;
  uint32_t _lastLunarTryMs = 0;
  // 农历任务
  TaskHandle_t _lunarTask = nullptr;
  SemaphoreHandle_t _lunarMutex = nullptr;
  volatile bool _lunarFetching = false;
  volatile bool _lunarReady = false;
  char _lunarResFull[48] = "";
  char _lunarResShort[32] = "";
  char _lunarResDate[16] = "";  // 取到结果时的公历日期
};
