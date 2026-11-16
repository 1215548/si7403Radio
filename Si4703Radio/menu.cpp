// menu.cpp -- 见 menu.h
#include "menu.h"
#include "pins.h"
#include "cn_font.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static MenuUi* s_instance = nullptr;

static const char* kMenuItems[] = {"WiFi配置", "收音机设置", "任务状态",
                                   "日期时间", "关于版本", "返回首页"};
#define MENU_COUNT 6

void MenuUi::begin(Led* led, Keys* keys, OledDisplay* oled, Si4703Radio* radio,
                   WifiPortal* wifi, Presets* presets) {
  _led = led;
  _keys = keys;
  _oled = oled;
  _radio = radio;
  _wifi = wifi;
  _presets = presets;
  s_instance = this;
  _radioOk = _radio->ok();
  _state = UiState::HOME;
  _dirty = true;
  // 频谱起始高度给个低基线,首帧不至于空一块;tickSpectrum 随后接管
  for (int i = 0; i < kSpecBars; i++) {
    _specLevel[i] = 2;
    _specPeak[i] = 2;
  }
}

bool MenuUi::playing() const {
  return _radioOk && !_radio->isMuted();
}

void MenuUi::showMsg(const char* msg) {
  snprintf(_msg, sizeof(_msg), "%s", msg);
  _msgUntil = millis() + 2000;
  _dirty = true;
}

void MenuUi::manualTune(float freq) {
  if (!_radioOk) return;
  if (_radio->tune(freq)) {
    _presets->setCurrent(-1);  // 手动调频后不再是预存台
    char buf[24];
    snprintf(buf, sizeof(buf), "TUNE %.2fMHz", _radio->freqMHz());
    showMsg(buf);
  }
}

// ---------- 轮询 ----------

static void printRdsLine(Si4703Radio* radio) {
  const RdsInfo& r = radio->rds();
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

// 显示用 RSSI(带迟滞):真实 RSSI 每秒 ±1~3 抖动,直接画会让信号条
// 和数字每秒都变、帧哈希每秒都变、每秒重发一次全屏——1 秒一闪的
// 触发器。迟滞后只有变化 >=3 才更新显示值,静止时零 SPI 流量。
int MenuUi::displayRssi() {
  int r = _radio->rssi();
  if (_dispRssi < 0 || abs(r - _dispRssi) >= 3) _dispRssi = r;
  return _dispRssi;
}

void MenuUi::pollRadio() {
  uint32_t now = millis();
  if (_radioOk && now - _lastRdsMs >= 50) {
    _lastRdsMs = now;
    if (_radio->pollRds()) {
      printRdsLine(_radio);
      _dirty = true;
    }
  }
  if (now - _lastStatusMs >= 1000) {
    _lastStatusMs = now;
    if (_radioOk) {
      if (_radio->readStatus()) {
        _led->set(_radio->isStereo() && !_radio->isMuted());
      }
    } else {
      _led->blinkError();
    }
    _dirty = true;  // 每秒刷新(RSSI/时钟秒数)
  }
  // (旧:RadioText 超长时滚动,每 600ms 刷一帧——底部已改频谱,不再滚动 RT)
}

void MenuUi::pollWifi() {
  _wifi->tick();
  if (_state == UiState::WIFI &&
      (uint32_t)(millis() - _lastRefreshMs) >= 2000) {
    _lastRefreshMs = millis();
    _dirty = true;  // WiFi 页 RSSI/状态刷新
  }
}

void MenuUi::tick() {
  _led->update();
  uint8_t pressed = _keys->update();
  if (pressed) {
    _led->blink(120);
    handleKeys(pressed);
  }
  pollRadio();
  pollWifi();
  tickSpectrum();  // 首页播放时底部频谱律动(只局部刷新底部)
  // 瞬时消息过期后清掉并重画中行(恢复显示台名),避免消息残留
  if (_msg[0] && millis() >= _msgUntil) {
    _msg[0] = '\0';
    _dirty = true;
  }
  if (_state == UiState::TASKS &&
      (uint32_t)(millis() - _lastTaskSnapMs) >= 2000) {
    snapshotTasks();
    _dirty = true;
  }
  if (_dirty) {
    drawCurrent();
    _dirty = false;
  }
}

// ---------- 按键 ----------

void MenuUi::handleKeys(uint8_t pressed) {
  switch (_state) {
    case UiState::HOME: homeKeys(pressed); break;
    case UiState::MENU: menuKeys(pressed); break;
    case UiState::WIFI: wifiKeys(pressed); break;
    case UiState::RADIOSET: radioSetKeys(pressed); break;
    case UiState::TASKS: tasksKeys(pressed); break;
    case UiState::DATETIME: pageKeys(pressed, _dtTop, 6); break;
    case UiState::ABOUT: pageKeys(pressed, _abTop, 6); break;
    case UiState::SCANNING: break;  // 搜台中按键由 runScan 内部处理
  }
}

void MenuUi::enterMenu() {
  if (playing() && !_bgPlay) {
    _radio->setMute(true);
    _resumeOnHome = true;
  }
  _state = UiState::MENU;
  _dirty = true;
}

void MenuUi::backHome() {
  if (_resumeOnHome) {
    _radio->setMute(false);
    _resumeOnHome = false;
  }
  _state = UiState::HOME;
  _dirty = true;
}

void MenuUi::homeKeys(uint8_t pressed) {
  if (pressed & KEY5_MASK) {
    enterMenu();
    return;
  }
  if (playing()) {
    char buf[32];
    if (pressed & KEY4_MASK) {
      if (_presets->count() > 0) {
        _presets->next(*_radio);
        snprintf(buf, sizeof(buf), "CH %d/%d %.2f", _presets->current() + 1,
                 _presets->count(), _radio->freqMHz());
      } else {
        float f = _radio->freqMHz() + RADIO_SPACE_MHZ;
        if (f > RADIO_BAND_HIGH_MHZ) f = RADIO_BAND_HIGH_MHZ;
        _radio->tune(f);
        snprintf(buf, sizeof(buf), "%.2fMHz", _radio->freqMHz());
      }
      showMsg(buf);
    }
    if (pressed & KEY3_MASK) {
      if (_presets->count() > 0) {
        _presets->prev(*_radio);
        snprintf(buf, sizeof(buf), "CH %d/%d %.2f", _presets->current() + 1,
                 _presets->count(), _radio->freqMHz());
      } else {
        float f = _radio->freqMHz() - RADIO_SPACE_MHZ;
        if (f < RADIO_BAND_LOW_MHZ) f = RADIO_BAND_LOW_MHZ;
        _radio->tune(f);
        snprintf(buf, sizeof(buf), "%.2fMHz", _radio->freqMHz());
      }
      showMsg(buf);
    }
    if (pressed & KEY2_MASK) {
      uint8_t v = _radio->volume();
      if (v < 15) _radio->setVolume(v + 1);
      snprintf(buf, sizeof(buf), "VOL %d", _radio->volume());
      showMsg(buf);
    }
    if (pressed & KEY1_MASK) {
      uint8_t v = _radio->volume();
      if (v > 0) _radio->setVolume(v - 1);
      snprintf(buf, sizeof(buf), "VOL %d", _radio->volume());
      showMsg(buf);
    }
  } else if (_wifi->timeValid()) {
    // 不播放且时间有效:KEY4/KEY3 切换时钟显示风格
    if (pressed & (KEY4_MASK | KEY3_MASK)) {
      _clockStyle ^= 1;
      _dirty = true;
    }
  }
  // 既不播放又无时间:KEY1~KEY4 暂不响应
}

void MenuUi::menuKeys(uint8_t pressed) {
  if (pressed & KEY3_MASK) {
    _menuSel = (_menuSel - 1 + MENU_COUNT) % MENU_COUNT;
    // 首尾循环时选中项可能跳到窗口外,两个方向都要把可见窗口拉回来
    if (_menuSel < _menuTop) _menuTop = _menuSel;
    if (_menuSel > _menuTop + 2) _menuTop = _menuSel - 2;
    _dirty = true;
  }
  if (pressed & KEY4_MASK) {
    _menuSel = (_menuSel + 1) % MENU_COUNT;
    if (_menuSel > _menuTop + 2) _menuTop = _menuSel - 2;
    if (_menuSel < _menuTop) _menuTop = _menuSel;
    _dirty = true;
  }
  if (pressed & KEY1_MASK) backHome();
  if (pressed & KEY5_MASK) {
    switch (_menuSel) {
      case 0: _state = UiState::WIFI; _wifiTop = 0; break;
      case 1: _state = UiState::RADIOSET; _rsTop = 0; break;
      case 2:
        _state = UiState::TASKS;
        _taskTop = 0;
        _lastTaskSnapMs = 0;
        snapshotTasks();
        break;
      case 3: _state = UiState::DATETIME; _dtTop = 0; break;
      case 4: _state = UiState::ABOUT; _abTop = 0; break;
      case 5: backHome(); break;
    }
    _dirty = true;
  }
}

void MenuUi::wifiKeys(uint8_t pressed) {
  if (pressed & KEY3_MASK && _wifiTop > 0) {
    _wifiTop--;
    _dirty = true;
  }
  if (pressed & KEY4_MASK && _wifiTop < 3) {
    _wifiTop++;
    _dirty = true;
  }
  if (pressed & KEY5_MASK) {
    _wifi->setPortal(!_wifi->portalOn());
    _dirty = true;
  }
  if (pressed & KEY1_MASK) {
    _state = UiState::MENU;
    _dirty = true;
  }
}

void MenuUi::radioSetKeys(uint8_t pressed) {
  const int kCount = 7;
  if (pressed & KEY3_MASK) {
    _rsSel = (_rsSel - 1 + kCount) % kCount;
    if (_rsSel < _rsTop) _rsTop = _rsSel;
    if (_rsSel > _rsTop + 2) _rsTop = _rsSel - 2;
    _dirty = true;
  }
  if (pressed & KEY4_MASK) {
    _rsSel = (_rsSel + 1) % kCount;
    if (_rsSel > _rsTop + 2) _rsTop = _rsSel - 2;
    if (_rsSel < _rsTop) _rsTop = _rsSel;
    _dirty = true;
  }
  if (pressed & KEY1_MASK) {
    _state = UiState::MENU;
    _dirty = true;
  }
  if (pressed & KEY5_MASK) execRadioSet(_rsSel);
}

void MenuUi::tasksKeys(uint8_t pressed) {
  int rows = _taskCount + 1;  // +1 行堆信息
  if (pressed & KEY3_MASK && _taskTop > 0) {
    _taskTop--;
    _dirty = true;
  }
  if (pressed & KEY4_MASK && _taskTop + 6 < rows) {
    _taskTop++;
    _dirty = true;
  }
  if (pressed & KEY1_MASK) {
    _state = UiState::MENU;
    _dirty = true;
  }
}

void MenuUi::pageKeys(uint8_t pressed, int& top, int lineCount) {
  if (pressed & KEY3_MASK && top > 0) {
    top--;
    _dirty = true;
  }
  if (pressed & KEY4_MASK && top + 3 < lineCount) {
    top++;
    _dirty = true;
  }
  if (pressed & KEY1_MASK) {
    _state = UiState::MENU;
    _dirty = true;
  }
}

void MenuUi::execRadioSet(int sel) {
  if (!_radioOk && sel != 5 && sel != 6) {
    showMsg("NO RADIO");
    _state = UiState::HOME;
    return;
  }
  switch (sel) {
    case 0: runScan(); break;  // 自动搜台
    case 1: {                  // 手动调频+
      float f = _radio->freqMHz() + RADIO_SPACE_MHZ;
      if (f > RADIO_BAND_HIGH_MHZ) f = RADIO_BAND_HIGH_MHZ;
      manualTune(f);
      break;
    }
    case 2: {  // 手动调频-
      float f = _radio->freqMHz() - RADIO_SPACE_MHZ;
      if (f < RADIO_BAND_LOW_MHZ) f = RADIO_BAND_LOW_MHZ;
      manualTune(f);
      break;
    }
    case 3: {  // 音量调整+
      uint8_t v = _radio->volume();
      if (v < 15) _radio->setVolume(v + 1);
      _dirty = true;
      break;
    }
    case 4: {  // 音量调整-
      uint8_t v = _radio->volume();
      if (v > 0) _radio->setVolume(v - 1);
      _dirty = true;
      break;
    }
    case 5:  // 后台播放 开/关
      _bgPlay = !_bgPlay;
      _dirty = true;
      break;
    case 6:  // 返回菜单:静音并返回菜单
      _radio->setMute(true);
      _resumeOnHome = false;
      _state = UiState::MENU;
      _dirty = true;
      break;
  }
}

// ---------- 自动搜台 ----------

static void onScanProgress(int found, float freq) {
  if (s_instance) s_instance->scanProgress(found, freq);
}

static bool onScanCancel() {
  if (!s_instance) return true;
  return s_instance->scanCancelCheck();
}

void MenuUi::scanProgress(int found, float freq) {
  _scanFound = found;
  _scanFreq = freq;
  drawScanning();
}

bool MenuUi::scanCancelCheck() {
  uint8_t p = _keys->update();
  return (p & KEY1_MASK) != 0;
}

void MenuUi::bootScan() {
  // 开机自动搜台:仅收音机正常且无预存台时跑一次。首次烧录 huge_app
  // 会清 NVS、预存台为空,不扫就停在默认 98.0MHz 无台可听;有预存台
  // 时 setup 里已调到第 1 台,不必重扫(全波段最长约 2 分钟)。
  // 搜台中按 KEY1 可取消;扫完自动回首页并出声。
  if (!_radioOk) return;
  if (_presets->count() > 0) return;
  runScan();
}

void MenuUi::runScan() {
  if (!_radioOk) {
    showMsg("NO RADIO");
    _state = UiState::HOME;
    return;
  }
  _state = UiState::SCANNING;
  _scanFound = 0;
  _scanFreq = RADIO_BAND_LOW_MHZ;
  drawScanning();
  int n = _presets->scan(*_radio, onScanProgress, onScanCancel);
  _scanFound = n;
  if (n > 0) {
    _radio->setMute(false);  // 搜到台就出声
    _resumeOnHome = false;
    char buf[32];
    snprintf(buf, sizeof(buf), "FOUND %d STATIONS", n);
    showMsg(buf);
  } else {
    showMsg("NO STATION");
  }
  _state = UiState::HOME;
  _dirty = true;
}

// ---------- 任务快照 ----------

void MenuUi::snapshotTasks() {
  _heapFree = ESP.getFreeHeap();
  UBaseType_t n = uxTaskGetNumberOfTasks();
  if (n == 0 || n > 16) {
    // 拿不到任务表(极少见):只显示本任务栈余量
    _taskCount = 1;
    snprintf(_tasks[0].name, sizeof(_tasks[0].name), "loopTask");
    _tasks[0].state = 'R';
    _tasks[0].stackFree = uxTaskGetStackHighWaterMark(NULL) * 4;
    _tasks[0].prio = 1;
    _lastTaskSnapMs = millis();
    return;
  }
  TaskStatus_t* arr = new TaskStatus_t[n];
  UBaseType_t got = uxTaskGetSystemState(arr, n, nullptr);
  _taskCount = 0;
  for (UBaseType_t i = 0; i < got && _taskCount < 16; i++) {
    snprintf(_tasks[_taskCount].name, sizeof(_tasks[_taskCount].name), "%s",
             arr[i].pcTaskName);
    switch (arr[i].eCurrentState) {
      case eRunning: _tasks[_taskCount].state = 'R'; break;
      case eReady: _tasks[_taskCount].state = 'r'; break;
      case eBlocked: _tasks[_taskCount].state = 'B'; break;
      case eSuspended: _tasks[_taskCount].state = 'S'; break;
      case eDeleted: _tasks[_taskCount].state = 'D'; break;
      default: _tasks[_taskCount].state = '?'; break;
    }
    _tasks[_taskCount].stackFree = arr[i].usStackHighWaterMark * 4;
    _tasks[_taskCount].prio = arr[i].uxCurrentPriority;
    _taskCount++;
  }
  delete[] arr;
  _lastTaskSnapMs = millis();
}

// ---------- 绘制 ----------

void MenuUi::drawCurrent() {
  switch (_state) {
    case UiState::HOME: drawHome(); break;
    case UiState::MENU: drawMenu(); break;
    case UiState::WIFI: drawWifi(); break;
    case UiState::RADIOSET: drawRadioSet(); break;
    case UiState::TASKS: drawTasks(); break;
    case UiState::DATETIME: drawDateTime(); break;
    case UiState::ABOUT: drawAbout(); break;
    case UiState::SCANNING: drawScanning(); break;
  }
}

void MenuUi::drawTitle(const char* zh, const char* rightEn) {
  _oled->fillRect(0, 0, 127, 15, true);
  _oled->textMixed(2, 0, zh, true);
  if (rightEn && rightEn[0]) {
    int w = (int)strlen(rightEn) * 5;  // Font8 宽 5px
    _oled->textEN(126 - w, 4, rightEn, &Font8, true);
  }
}

void MenuUi::drawList(const char* title, const char* items[], int count,
                      int sel, int top, const char* rightEn) {
  _oled->prepare();
  drawTitle(title, rightEn);
  for (int i = 0; i < 3; i++) {
    int idx = top + i;
    if (idx >= count) break;
    int y = 16 + i * 16;
    bool isSel = (idx == sel);
    // 选中行反白到 x=124,右侧留 2px 给滚动条,避免滚动条被反白吞掉
    if (isSel) _oled->fillRect(0, y, 124, y + 15, true);
    char tmp[48];
    CnFont_truncate(items[idx], 116, tmp, sizeof(tmp));
    _oled->textMixed(2, y, tmp, isSel);
  }
  drawScrollbar(top, 3, count);
  _oled->flush();
}

void MenuUi::drawScrollbar(int top, int visible, int count) {
  if (count <= visible) return;
  const int x0 = 126, x1 = 127;
  const int y0 = 17, y1 = 62;
  int track = y1 - y0 + 1;
  int thumb = track * visible / count;
  if (thumb < 6) thumb = 6;
  int span = count - visible;
  if (top < 0) top = 0;
  if (top > span) top = span;
  int ty = y0 + (track - thumb) * top / span;
  _oled->fillRect(x0, ty, x1, ty + thumb - 1, true);
}

void MenuUi::drawHome() {
  if (playing()) {
    drawHomePlaying();
  } else if (_wifi->timeValid()) {
    drawHomeClock();
  } else {
    drawHomeReserved();
  }
}

void MenuUi::drawHomePlaying() {
  char buf[48];
  _oled->prepare();
  // 顶行(状态放顶部,Font8):RSSI + 立体声/音量/台序号 + 时间
  // (用户要求 RSSI/ST/VOL 放顶部;台名改到 y=42 中行)
  int rssi = displayRssi();
  if (_presets->count() > 0) {
    snprintf(buf, sizeof(buf), "RSSI %d %s V%d %d/%d", rssi,
             _radio->isStereo() ? "ST" : "MO", _radio->volume(),
             _presets->current() + 1, _presets->count());
  } else {
    snprintf(buf, sizeof(buf), "RSSI %d %s V%d", rssi,
             _radio->isStereo() ? "ST" : "MO", _radio->volume());
  }
  _oled->textEN(2, 2, buf, &Font8);  // Font8 居中于 12px 顶行(y=2..9)
  struct tm t;
  if (_wifi->getTime(t)) {
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    int w = (int)strlen(buf) * 5;  // Font8 宽 5px
    _oled->textEN(126 - w, 2, buf, &Font8);
  }
  _oled->hline(0, 127, 14);
  // 大频率
  snprintf(buf, sizeof(buf), "%.2f", _radio->freqMHz());
  _oled->textEN(2, 16, buf, &Font24);
  _oled->textEN(2 + (int)strlen(buf) * 17 + 2, 28, "MHz", &Font12);
  // 中行:瞬时消息(如 CH/VOL/FOUND)优先,否则显示台名
  uint32_t now = millis();
  if (now < _msgUntil && _msg[0]) {
    _oled->textEN(2, 42, _msg, &Font8);
  } else {
    const RdsInfo& r = _radio->rds();
    _oled->textEN(2, 42, r.psComplete ? r.ps : "FM", &Font8);
  }
  // 底部(y=52..63):频谱律动,全屏重画时也画上,避免每秒状态刷新
  // 把底部清空一帧;平移动画由 tickSpectrum 只局部刷新这两页。
  drawSpectrum();
  _oled->flush();
}

// ---------- 底部频谱 ----------
// 16 根柱子:x = i*8,6px 宽、2px 间隔,底部对齐 y=63。
// Si4703 不输出音频电平,所以是装饰性律动:两个不同频率的正弦
// 叠加,相邻柱子相位错开形成左右流动的波;20fps 下上下都平滑。
void MenuUi::drawSpectrum() {
  for (int i = 0; i < kSpecBars; i++) {
    int x = i * 8;
    int h = _specLevel[i];
    if (h > 0) _oled->fillRect(x, 64 - h, x + 5, 63, true);
    int pk = _specPeak[i];
    // 峰值保持:只在柱子上方 1px 画一条短线
    if (pk > h) _oled->fillRect(x, 64 - pk, x + 5, 64 - pk, true);
  }
}

void MenuUi::tickSpectrum() {
  if (_state != UiState::HOME || !playing()) return;
  uint32_t now = millis();
  if (now - _lastSpecMs < 50) return;  // 20fps
  _lastSpecMs = now;
  float t = (float)now * 0.001f;
  for (int i = 0; i < kSpecBars; i++) {
    float v = 0.55f + 0.30f * sinf(t * 2.6f + (float)i * 0.72f) +
              0.15f * sinf(t * 5.3f + (float)i * 1.31f);
    int h = 1 + (int)(11.0f * v);
    if (h < 1) h = 1;
    if (h > 12) h = 12;
    _specLevel[i] = (uint8_t)h;
    if (h >= _specPeak[i]) {
      _specPeak[i] = (uint8_t)h;
    } else {
      int pk = _specPeak[i] - 1;  // 20fps 下约 0.5s 落完
      _specPeak[i] = (uint8_t)(pk < h ? h : pk);
    }
  }
  _oled->clearRegion(0, 52, 127, 63);
  drawSpectrum();
  // 逻辑 y=52..63 跨逻辑页 6/7;refreshRows 内部换算成物理页 1/0
  _oled->refreshRows(52, 63);
}

void MenuUi::drawHomeClock() {
  struct tm t;
  if (!_wifi->getTime(t)) {
    drawHomeReserved();
    return;
  }
  char buf[48];
  _oled->prepare();
  if (_clockStyle == 0) {
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    _oled->textEN(2, 2, buf, &Font24);
    snprintf(buf, sizeof(buf), ":%02d", t.tm_sec);
    _oled->textEN(89, 14, buf, &Font12);
    _oled->hline(0, 127, 28);
    snprintf(buf, sizeof(buf), "%d月%d日", t.tm_mon + 1, t.tm_mday);
    _oled->textMixed(2, 32, buf);
    if (_wifi->lunarValid()) {
      snprintf(buf, sizeof(buf), "%s %s", _wifi->weekDayName(),
               _wifi->lunarShort().c_str());
    } else {
      snprintf(buf, sizeof(buf), "%s", _wifi->weekDayName());
    }
    _oled->textMixed(2, 48, buf);
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min,
             t.tm_sec);
    _oled->textEN(2, 8, buf, &Font16);
    _oled->hline(0, 127, 28);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday);
    _oled->textEN(2, 32, buf, &Font16);
    _oled->textMixed(2, 48,
                     _wifi->lunarValid() ? _wifi->lunarFull().c_str()
                                         : "农历未获取");
  }
  _oled->flush();
}

void MenuUi::drawHomeReserved() {
  _oled->prepare();
  _oled->textEN(2, 8, "Si4703 FM", &Font16);
  _oled->textMixed(2, 32, "功能预留");
  _oled->textMixed(2, 48, "KEY5 进入菜单");
  _oled->flush();
}

// ---------- 主菜单图标(14x14,x=3,y=行顶+1) ----------
static void iconWifi(OledDisplay* o, int x, int y, bool c) {
  const float kPi = 3.14159265f;
  int cx = x + 7, cy = y + 11;
  o->fillRect(cx - 1, cy - 1, cx + 1, cy, c);  // 圆点
  for (int r = 4; r <= 8; r += 4) {            // 两道弧
    for (int deg = 215; deg <= 325; deg += 6) {
      float a = (float)deg * kPi / 180.0f;
      o->pixel(cx + (int)lroundf(r * cosf(a)),
               cy + (int)lroundf(r * sinf(a)), c);
    }
  }
}

static void iconRadio(OledDisplay* o, int x, int y, bool c) {
  o->line(x + 2, y + 10, x + 9, y + 2, c);       // 天线
  o->fillRect(x + 8, y + 2, x + 10, y + 3, c);   // 天线头
  o->rect(x + 1, y + 6, x + 11, y + 11, c);      // 机身
  o->fillRect(x + 3, y + 8, x + 4, y + 9, c);    // 旋钮
  o->fillRect(x + 7, y + 8, x + 10, y + 9, c);   // 喇叭格栅
}

static void iconTasks(OledDisplay* o, int x, int y, bool c) {
  o->fillRect(x + 1, y + 3, x + 11, y + 4, c);
  o->fillRect(x + 1, y + 6, x + 8, y + 7, c);
  o->fillRect(x + 1, y + 9, x + 10, y + 10, c);
}

static void iconClock(OledDisplay* o, int x, int y, bool c) {
  o->circle(x + 7, y + 7, 5, false, c);
  o->line(x + 7, y + 7, x + 7, y + 4, c);
  o->line(x + 7, y + 7, x + 9, y + 8, c);
}

static void iconInfo(OledDisplay* o, int x, int y, bool c) {
  o->circle(x + 7, y + 7, 5, false, c);
  o->pixel(x + 7, y + 4, c);
  o->line(x + 7, y + 6, x + 7, y + 10, c);
}

static void iconHome(OledDisplay* o, int x, int y, bool c) {
  o->line(x + 1, y + 7, x + 7, y + 1, c);
  o->line(x + 7, y + 1, x + 13, y + 7, c);
  o->rect(x + 2, y + 7, x + 12, y + 12, c);
  o->rect(x + 5, y + 9, x + 7, y + 12, c);  // 门
}

static void drawMenuIcon(int idx, OledDisplay* o, int x, int y, bool c) {
  switch (idx) {
    case 0: iconWifi(o, x, y, c); break;
    case 1: iconRadio(o, x, y, c); break;
    case 2: iconTasks(o, x, y, c); break;
    case 3: iconClock(o, x, y, c); break;
    case 4: iconInfo(o, x, y, c); break;
    default: iconHome(o, x, y, c); break;
  }
}

void MenuUi::drawMenu() {
  char rightEn[20] = "";
  struct tm t;
  bool hasTime = _wifi->getTime(t);
  if (playing()) {
    // 后台播放时标题栏右侧显示频率,有 NTP 再带上时间
    if (hasTime) {
      snprintf(rightEn, sizeof(rightEn), "FM%.1f %02d:%02d",
               _radio->freqMHz(), t.tm_hour, t.tm_min);
    } else {
      snprintf(rightEn, sizeof(rightEn), "FM%.1f", _radio->freqMHz());
    }
  } else if (hasTime) {
    snprintf(rightEn, sizeof(rightEn), "%02d:%02d", t.tm_hour, t.tm_min);
  } else if (!_wifi->connected()) {
    snprintf(rightEn, sizeof(rightEn), "NO WIFI");
  }
  _oled->prepare();
  drawTitle("菜单", rightEn);
  for (int i = 0; i < 3; i++) {
    int idx = _menuTop + i;
    if (idx >= MENU_COUNT) break;
    int y = 16 + i * 16;
    bool isSel = (idx == _menuSel);
    if (isSel) _oled->fillRect(0, y, 124, y + 15, true);
    drawMenuIcon(idx, _oled, 3, y + 2, !isSel);
    char tmp[48];
    CnFont_truncate(kMenuItems[idx], 98, tmp, sizeof(tmp));
    _oled->textMixed(22, y, tmp, isSel);
  }
  drawScrollbar(_menuTop, 3, MENU_COUNT);
  _oled->flush();
}

void MenuUi::drawWifi() {
  struct WiLine {
    char text[40];
    bool ascii12;
  };
  WiLine lines[6];
  int n = 0;
  snprintf(lines[n].text, sizeof(lines[n].text), "状态: %s",
           _wifi->connected() ? "已连接" : "未连接");
  lines[n++].ascii12 = false;
  {
    char ssid[16];
    snprintf(ssid, sizeof(ssid), "%.12s", _wifi->ssid().c_str());
    snprintf(lines[n].text, sizeof(lines[n].text), "SSID: %s", ssid);
    lines[n++].ascii12 = true;
  }
  snprintf(lines[n].text, sizeof(lines[n].text), "IP: %s",
           _wifi->ip().c_str());
  lines[n++].ascii12 = true;
  if (_wifi->connected()) {
    snprintf(lines[n].text, sizeof(lines[n].text), "信号: %ddBm",
             _wifi->rssi());
  } else {
    snprintf(lines[n].text, sizeof(lines[n].text), "信号: -");
  }
  lines[n++].ascii12 = false;
  snprintf(lines[n].text, sizeof(lines[n].text), "网页: %s",
           _wifi->portalOn() ? "已开启" : "已关闭");
  lines[n++].ascii12 = false;
  if (_wifi->portalOn()) {
    snprintf(lines[n].text, sizeof(lines[n].text), "AP 192.168.4.1");
    lines[n++].ascii12 = true;
  } else {
    snprintf(lines[n].text, sizeof(lines[n].text), "按KEY5开启");
    lines[n++].ascii12 = false;
  }
  _oled->prepare();
  drawTitle("WiFi配置", _wifi->portalOn() ? "AP ON" : "AP OFF");
  for (int i = 0; i < 3; i++) {
    int idx = _wifiTop + i;
    if (idx >= n) break;
    int y = 16 + i * 16;
    if (lines[idx].ascii12) {
      _oled->textEN12(2, y + 2, lines[idx].text);
    } else {
      _oled->textMixed(2, y, lines[idx].text);
    }
  }
  if (_wifiTop > 0) _oled->textEN(120, 18, "^", &Font8);
  if (_wifiTop + 3 < n) _oled->textEN(120, 50, "v", &Font8);
  _oled->flush();
}

void MenuUi::drawRadioSet() {
  char b0[32], b1[32], b2[32], b3[32], b4[32], b5[32], b6[32];
  snprintf(b0, sizeof(b0), "自动搜台");
  snprintf(b1, sizeof(b1), "手动调频+");
  snprintf(b2, sizeof(b2), "手动调频-");
  snprintf(b3, sizeof(b3), "音量调整+ %d", _radio->volume());
  snprintf(b4, sizeof(b4), "音量调整- %d", _radio->volume());
  snprintf(b5, sizeof(b5), "后台播放 %s", _bgPlay ? "开" : "关");
  snprintf(b6, sizeof(b6), "返回菜单");
  const char* items[7] = {b0, b1, b2, b3, b4, b5, b6};
  drawList("收音机设置", items, 7, _rsSel, _rsTop, "");
}

void MenuUi::drawTasks() {
  _oled->prepare();
  char title[24];
  snprintf(title, sizeof(title), "任务状态");
  drawTitle(title, "");
  char buf[48];
  int rows = _taskCount + 1;
  for (int i = 0; i < 6; i++) {
    int idx = _taskTop + i;
    if (idx >= rows) break;
    int y = 16 + i * 8;
    if (idx == 0) {
      snprintf(buf, sizeof(buf), "Heap %luB N=%d", _heapFree, _taskCount);
    } else {
      const TaskRow& r = _tasks[idx - 1];
      snprintf(buf, sizeof(buf), "%-12s %c %5lu %u", r.name, r.state,
               (unsigned long)r.stackFree, r.prio);
    }
    _oled->textEN(2, y, buf, &Font8);
  }
  if (_taskTop > 0) _oled->textEN(120, 18, "^", &Font8);
  if (_taskTop + 6 < rows) _oled->textEN(120, 58, "v", &Font8);
  _oled->flush();
}

void MenuUi::drawDateTime() {
  const char* lines[6];
  char l0[24], l1[24], l4[40];
  struct tm t;
  if (!_wifi->timeValid()) {
    static const char* noTime[] = {"时间未同步", "请连接 WiFi",
                                   "再等 NTP 校时"};
    drawList("日期时间", noTime, 3, -1, 0, "");
    return;
  }
  _wifi->getTime(t);
  snprintf(l0, sizeof(l0), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  snprintf(l1, sizeof(l1), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1,
           t.tm_mday);
  snprintf(l4, sizeof(l4), "%s",
           _wifi->lunarValid() ? _wifi->lunarFull().c_str() : "农历未获取");
  lines[0] = l0;
  lines[1] = l1;
  lines[2] = _wifi->weekDayName();
  lines[3] = "农历";
  lines[4] = l4;
  lines[5] = _wifi->connected() ? "NTP 已同步" : "NTP 未同步";
  // 日期时间页不设选中行(sel=-1),只滚动
  _oled->prepare();
  drawTitle("日期时间", "");
  for (int i = 0; i < 3; i++) {
    int idx = _dtTop + i;
    if (idx >= 6) break;
    _oled->textMixed(2, 16 + i * 16, lines[idx]);
  }
  if (_dtTop > 0) _oled->textEN(120, 18, "^", &Font8);
  if (_dtTop + 3 < 6) _oled->textEN(120, 50, "v", &Font8);
  _oled->flush();
}

void MenuUi::drawAbout() {
  char l0[24], l2[24], l3[24];
  snprintf(l0, sizeof(l0), "固件 %s", FW_VERSION);
  snprintf(l2, sizeof(l2), "%s", __DATE__);
  snprintf(l3, sizeof(l3), "%s", __TIME__);
  const char* lines[6] = {l0, "编译时间", l2, l3, "Si4703 FM", "ESP32-C3"};
  _oled->prepare();
  drawTitle("关于版本", "");
  for (int i = 0; i < 3; i++) {
    int idx = _abTop + i;
    if (idx >= 6) break;
    _oled->textMixed(2, 16 + i * 16, lines[idx]);
  }
  if (_abTop > 0) _oled->textEN(120, 18, "^", &Font8);
  if (_abTop + 3 < 6) _oled->textEN(120, 50, "v", &Font8);
  _oled->flush();
}

void MenuUi::drawScanning() {
  char buf[32];
  _oled->prepare();
  drawTitle("自动搜台", "");
  snprintf(buf, sizeof(buf), "已找到 %d 个台", _scanFound);
  _oled->textMixed(2, 16, buf);
  snprintf(buf, sizeof(buf), "当前 %.2fMHz", _scanFreq);
  _oled->textMixed(2, 32, buf);
  _oled->textMixed(2, 48, "按KEY1取消");
  _oled->flush();
}
