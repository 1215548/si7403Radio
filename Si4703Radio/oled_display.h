// oled_display.h -- OLED 显示模块(SSD1309 128x64,单色黄)
// 底层是仓库原有的 Waveshare 驱动(DEV_Config/OLED_Driver/GUI_Paint,
// 已按本工程引脚改过,见 DEV_Config.h);本模块只负责底层画布与通用
// 图元,具体界面由 menu.cpp 组合(首页/菜单/各子页),见 menu.h。
// 逻辑画布 128x64(旋转 270°)。
#pragma once
#include <Arduino.h>
#include "fonts.h"

class OledDisplay {
public:
  // 初始化 SPI + 复位 + 显存,并画出 Linux 风格开机画面。
  // 写-only SPI 无法回读,只要代码跑到这里就返回 true;屏亮不亮
  // 要看硬件(电源/复位/接线),见 README。
  bool begin(const char* version);
  void clear();   // 清屏并立即刷新
  void prepare(); // 清显存(不刷新,画完一组图元后调 flush)
  void flush();   // 把显存刷到屏上

  // ---- 开机动画(页对齐:标头页 0 / 日志页 1..6 / 进度页 7) ----
  // state: 0=进行中(..) 1=成功(OK) 2=失败(!!)
  void bootLog(const char* msg, int state);
  void bootUpdateLast(bool ok);  // 把最后一条 .. 改成 OK/!!
  void bootProgress(int pct);    // 底部进度条 0..100
  void bootAnimateProgress(int from, int to, uint32_t ms);
  void bootFinish();             // 100% + "root@si4703:~# _" 光标闪烁

  // ---- 通用图元(逻辑坐标 128x64) ----
  // ASCII 字符串;invert=true 为反白(黑字白底)
  void textEN(int x, int y, const char* s, sFONT* font, bool invert = false);
  // UTF-8 混排:中文 16 点阵 + ASCII Font16;invert=true 为反白
  void textMixed(int x, int y, const char* utf8, bool invert = false);
  int mixedWidth(const char* utf8);
  // 12px 紧凑混排:汉字 12x12(16 点阵降采样)+ ASCII Font8
  void textMixed12(int x, int y, const char* utf8, bool invert = false);
  int mixedWidth12(const char* utf8);
  // ASCII 字符串(Font12,居中在 16px 行内,常用于 SSID/IP 行)
  void textEN12(int x, int y, const char* s, bool invert = false);
  void hline(int x0, int x1, int y);
  void fillRect(int x0, int y0, int x1, int y1, bool white);
  void rect(int x0, int y0, int x1, int y1, bool white = true);
  void pixel(int x, int y, bool white = true);
  void line(int x0, int y0, int x1, int y1, bool white = true);
  void circle(int cx, int cy, int r, bool fill, bool white = true);
  // 信号条:外框 + 按 pct(0-100)填充
  void bar(int x, int y, int w, int h, int pct);
  // 只清显存里一个矩形区域(不刷新);频谱动画逐帧重画底部用
  void clearRegion(int x0, int y0, int x1, int y1);
  // 局部刷新逻辑行区间 [y0,y1](0..63):只把覆盖到的页发到屏上,
  // 不碰其他页。内部会把逻辑页换算成旋转 270° 后的物理页。
  void refreshRows(int y0, int y1);

  // 通用两行消息(启动画面、搜台中、按键反馈等)
  void showMessage(const char* line1, const char* line2);
  // 阶段一自检图:几何图形 + 字符,确认屏/字库/旋转都正常
  void showTestPattern();

private:
  uint32_t _lastHash = 0;  // 上次发出帧的 FNV-1a 哈希(防频闪,见 .cpp flush)
  bool _hashValid = false;
  uint32_t frameHash() const;  // 显存 FNV-1a 哈希

  // 开机画面内部状态
  static const int kBootRows = 6;
  char _bootLines[kBootRows][26] = {{0}};
  int _bootCount = 0;
  int _bootPct = 0;
  uint32_t _bootStartMs = 0;
  void bootDrawHeader(const char* version);
  void bootDrawProgressBar();
  void bootDrawLogs();
  void bootDrawLogRow(int row);
  void bootDrawPrompt(bool cursorOn);
};
