// menu.h -- 菜单与界面状态机
// 界面风格:纯文本菜单(ASCII 风格),标题栏反白 + 16px 中文行 + 滚动窗口。
// 状态:
//   HOME     首页(收音播放 / 时钟 / 预留三态)
//   MENU     主菜单:WiFi配置/收音机设置/任务状态/日期时间/关于版本/返回首页
//   WIFI     WiFi 配置页(状态 + KEY5 开关网页配置服务)
//   RADIOSET 收音机设置页(自动搜台/手动调频/音量/后台播放/返回菜单)
//   TASKS    任务状态页(FreeRTOS 任务 + 栈余量)
//   DATETIME 日期时间页(NTP 时间 + 日历 + 农历)
//   ABOUT    关于版本页(固件版本 + 编译时间)
//   SCANNING 自动搜台进行中(进度 + KEY1 取消)
// 按键(菜单内统一):KEY3 上、KEY4 下、KEY5 确认/执行、KEY1 返回。
// 首页:KEY5 进菜单;播放时 KEY4/K3 切台(有预存)或调频(无预存),
//       KEY2/K1 音量;不播放且时间有效时 KEY4/K3 切换时钟风格。
#pragma once
#include <Arduino.h>
#include "led.h"
#include "keys.h"
#include "oled_display.h"
#include "radio_si4703.h"
#include "wifi_portal.h"
#include "presets.h"

enum class UiState : uint8_t {
  HOME,
  MENU,
  WIFI,
  RADIOSET,
  TASKS,
  DATETIME,
  ABOUT,
  SCANNING,
};

class MenuUi {
public:
  void begin(Led* led, Keys* keys, OledDisplay* oled, Si4703Radio* radio,
             WifiPortal* wifi, Presets* presets);
  void tick();  // 每轮 loop 调一次:按键 + 收音轮询 + 刷新
  // 供串口命令用(见 .ino)
  Si4703Radio* radio() { return _radio; }
  Presets* presets() { return _presets; }
  void refresh() { _dirty = true; }
  void setRadioOk(bool ok) { _radioOk = ok; _dirty = true; }
  void showMsg(const char* msg);  // 瞬时消息(首页 RT 行显示 2 秒,限 ASCII)
  void manualTune(float freq);    // 串口 f 命令:调频 + 清预存台序号
  // 开机自动搜台:仅收音机正常且无预存台时跑一次(见 .cpp bootScan)
  void bootScan();
  // 供自动搜台回调用(见 menu.cpp 底部静态函数)
  void scanProgress(int found, float freq);
  bool scanCancelCheck();

private:
  // ---- 轮询 ----
  int displayRssi();  // 显示用 RSSI(带迟滞,防每秒抖动重画)
  void drawBottomArea();  // 底部频谱/瞬时消息(y=52..63)
  void tickSpectrum();    // 频谱动画推进 + 局部刷新
  void pollRadio();   // 1s 读状态 + 50ms RDS + LED
  void pollWifi();    // wifi.tick() + 时钟刷新节流
  // ---- 按键分发 ----
  void handleKeys(uint8_t pressed);
  void homeKeys(uint8_t pressed);
  void menuKeys(uint8_t pressed);
  void wifiKeys(uint8_t pressed);
  void radioSetKeys(uint8_t pressed);
  void tasksKeys(uint8_t pressed);
  void pageKeys(uint8_t pressed, int& top, int lineCount);
  void enterMenu();
  void backHome();
  void execRadioSet(int sel);
  void runScan();  // 自动搜台(阻塞,进度 + KEY1 取消)
  // ---- 绘制 ----
  void drawCurrent();
  void drawHome();
  void drawHomePlaying();
  void drawHomeClock();
  void drawHomeReserved();
  void drawMenu();
  void drawWifi();
  void drawRadioSet();
  void drawTasks();
  void drawDateTime();
  void drawAbout();
  void drawScanning();
  void drawTitle(const char* zh, const char* rightEn);
  // 列表菜单:标题 + 3 行可见(16px),sel 选中行反白;返回是否需要滚动
  void drawList(const char* title, const char* items[], int count, int sel,
                int top, const char* rightEn);
  bool playing() const;  // 收音机正在播放(OK 且未静音)
  void setDirty() { _dirty = true; }

  Led* _led = nullptr;
  Keys* _keys = nullptr;
  OledDisplay* _oled = nullptr;
  Si4703Radio* _radio = nullptr;
  WifiPortal* _wifi = nullptr;
  Presets* _presets = nullptr;

  UiState _state = UiState::HOME;
  bool _dirty = true;
  bool _radioOk = false;
  // 导航
  int _menuSel = 0, _menuTop = 0;
  int _rsSel = 0, _rsTop = 0;
  int _wifiTop = 0, _taskTop = 0, _dtTop = 0, _abTop = 0;
  int _clockStyle = 0;   // 首页时钟风格 0/1
  bool _bgPlay = true;   // 后台播放:进菜单是否保持出声
  bool _resumeOnHome = false;  // 进菜单时静音了,回首页要恢复
  // 瞬时消息
  char _msg[48] = "";
  uint32_t _msgUntil = 0;
  // 搜台进度
  int _scanFound = 0;
  float _scanFreq = 0;
  // 轮询节流
  uint32_t _lastStatusMs = 0;
  uint32_t _lastRdsMs = 0;
  uint32_t _lastRefreshMs = 0;
  // 显示用 RSSI(带迟滞,见 .cpp displayRssi):真实 RSSI 每秒都在
  // ±1~3 抖动,直接画会让信号条/数字每秒都变、帧哈希每秒都变、
  // 每秒重发一次全屏——这就是 1 秒一闪。迟滞后只有信号真正变化
  // (>=3)才更新显示,静止时零 SPI 流量。
  int _dispRssi = -1;
  // 底部频谱(伪频谱,见 .cpp drawBottomArea/tickSpectrum)
  static const int kSpecBars = 16;
  uint8_t _specLevel[kSpecBars] = {0};
  uint8_t _specPeak[kSpecBars] = {0};
  uint32_t _lastSpecMs = 0;
  uint32_t _specTick = 0;
  // 任务快照
  struct TaskRow {
    char name[17];
    char state;  // R/r/B/S/D
    uint32_t stackFree;
    uint8_t prio;
  };
  TaskRow _tasks[16];
  int _taskCount = 0;
  uint32_t _heapFree = 0;
  uint32_t _lastTaskSnapMs = 0;
  void snapshotTasks();
};
