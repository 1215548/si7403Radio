// oled_display.cpp -- 见 oled_display.h
#include "oled_display.h"
#include "cn_font.h"
#include "OLED_Driver.h"
#include "GUI_Paint.h"
#include "DEV_Config.h"
#include "ImageData.h"
#include <stdio.h>
#include <string.h>

// 显存:64 列字节 x 128 行,与 Waveshare 例程的算法一致
// (Paint_NewImage(BlackImage, 64, 128, 270),逻辑画布 128x64)
static UBYTE s_frame[((OLED_2IN42_WIDTH % 8 == 0)
                          ? (OLED_2IN42_WIDTH / 8)
                          : (OLED_2IN42_WIDTH / 8 + 1)) *
                     OLED_2IN42_HEIGHT];

bool OledDisplay::begin(const char* version) {
  System_Init();
  OLED_2IN42_Init();
  Driver_Delay_ms(500);
  OLED_2IN42_Clear();
  Paint_NewImage(s_frame, OLED_2IN42_WIDTH, OLED_2IN42_HEIGHT, 270, BLACK);
  Paint_SelectImage(s_frame);
  Paint_Clear(BLACK);
  bootDrawHeader(version);
  bootDrawProgressBar();
  flush();  // 首帧整屏发一次,之后开机动画全部按页局部刷新
  // 全屏第一页偶发被吞,标头单独补发一次(物理页 7,128 字节)
  refreshRows(0, 7);
  _bootStartMs = millis();
  return true;
}

void OledDisplay::clear() {
  Paint_Clear(BLACK);
  flush();
}

void OledDisplay::prepare() { Paint_Clear(BLACK); }

uint32_t OledDisplay::frameHash() const {
  uint32_t h = 2166136261u;  // FNV-1a 32
  for (size_t i = 0; i < sizeof(s_frame); i++) {
    h ^= s_frame[i];
    h *= 16777619u;
  }
  return h;
}

void OledDisplay::flush() {
  // 防频闪:帧内容与上次发出时完全相同就不调驱动——静止画面零 SPI
  // 流量。变了则全屏重发(逐页突发,约 1ms,两种寻址模式下都对)。
  // 局部刷新曾因横向寻址下页地址命令被忽略而错页,已改回全屏重发。
  uint32_t h = frameHash();
  if (_hashValid && h == _lastHash) return;
  _lastHash = h;
  _hashValid = true;
  OLED_2IN42_Display(s_frame);
  // 实机:从频谱局部刷新切到整屏页面时,全屏第一页(屏幕最底 8px)
  // 偶发被吞,底部残留上一屏内容。全屏末尾再补发底部两页(物理页
  // 1/0,倒序写,页 0 最后),保证切页时底部立即更新;只需 256 字节。
  OLED_2IN42_RefreshPages(s_frame, 0, 1);
}

void OledDisplay::clearRegion(int x0, int y0, int x1, int y1) {
  Paint_DrawRectangle((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1, BLACK,
                      DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void OledDisplay::refreshRows(int y0, int y1) {
  if (y0 > y1) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (y0 < 0) y0 = 0;
  if (y1 > 63) y1 = 63;
  int lp0 = y0 / 8;
  int lp1 = y1 / 8;
  // 显存逻辑页 lp 在 GDDRAM 物理页 7-lp(见 OLED_2IN42_Display 的
  // (7-page) 下标)。区间要反过来映射:lp0..lp1 -> (7-lp1)..(7-lp0)。
  OLED_2IN42_RefreshPages(s_frame, 7 - lp1, 7 - lp0);
  // 局部刷新后屏上内容与显存一致,同步哈希,避免下一次 flush 误判后
  // 把整屏又重发一遍。
  _lastHash = frameHash();
  _hashValid = true;
}

void OledDisplay::textEN(int x, int y, const char* s, sFONT* font,
                         bool invert) {
  Paint_DrawString_EN((UWORD)x, (UWORD)y, s, font, invert ? BLACK : WHITE,
                      invert ? WHITE : BLACK);
}

void OledDisplay::textMixed(int x, int y, const char* utf8, bool invert) {
  CnFont_drawMixed(x, y, utf8, invert);
}

int OledDisplay::mixedWidth(const char* utf8) {
  return CnFont_mixedWidth(utf8);
}

void OledDisplay::textMixed12(int x, int y, const char* utf8, bool invert) {
  CnFont_drawMixed12(x, y, utf8, invert);
}

int OledDisplay::mixedWidth12(const char* utf8) {
  return CnFont_mixedWidth12(utf8);
}

void OledDisplay::textEN12(int x, int y, const char* s, bool invert) {
  Paint_DrawString_EN((UWORD)x, (UWORD)y, s, &Font12, invert ? BLACK : WHITE,
                      invert ? WHITE : BLACK);
}

void OledDisplay::hline(int x0, int x1, int y) {
  Paint_DrawLine((UWORD)x0, (UWORD)y, (UWORD)x1, (UWORD)y, WHITE,
                 DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void OledDisplay::fillRect(int x0, int y0, int x1, int y1, bool white) {
  Paint_DrawRectangle((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1,
                      white ? WHITE : BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void OledDisplay::rect(int x0, int y0, int x1, int y1, bool white) {
  Paint_DrawRectangle((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1,
                      white ? WHITE : BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
}

void OledDisplay::pixel(int x, int y, bool white) {
  Paint_DrawPoint((UWORD)x, (UWORD)y, white ? WHITE : BLACK, DOT_PIXEL_1X1,
                  DOT_STYLE_DFT);
}

void OledDisplay::line(int x0, int y0, int x1, int y1, bool white) {
  Paint_DrawLine((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1,
                 white ? WHITE : BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void OledDisplay::circle(int cx, int cy, int r, bool fill, bool white) {
  Paint_DrawCircle((UWORD)cx, (UWORD)cy, (UWORD)r, white ? WHITE : BLACK,
                   DOT_PIXEL_1X1, fill ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);
}

void OledDisplay::bar(int x, int y, int w, int h, int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  Paint_DrawRectangle((UWORD)x, (UWORD)y, (UWORD)(x + w - 1),
                      (UWORD)(y + h - 1), WHITE, DOT_PIXEL_1X1,
                      DRAW_FILL_EMPTY);
  int fw = (w - 2) * pct / 100;
  if (fw > 0) {
    Paint_DrawRectangle((UWORD)(x + 1), (UWORD)(y + 1),
                        (UWORD)(x + fw), (UWORD)(y + h - 2), WHITE,
                        DOT_PIXEL_1X1, DRAW_FILL_FULL);
  }
}

// ---------- 开机动画 ----------
// 屏幕按 8px 页对齐:
//   y=0..7   标头(反白):SI4703 RADIO OS + 版本
//   y=8..55  6 行内核日志(每行 8px,正好一页)
//   y=56..63 进度条 / 最后变成 shell 提示符
// 首帧整屏刷一次,之后日志/进度/提示都只刷新对应页。
void OledDisplay::bootDrawHeader(const char* version) {
  fillRect(0, 0, 127, 8, true);  // 反白标头,填 y=0..7
  textEN(1, 0, "SI4703 RADIO OS", &Font8, true);
  if (version && version[0]) {
    int w = (int)strlen(version) * 5;
    textEN(126 - w, 0, version, &Font8, true);
  }
}

void OledDisplay::bootDrawProgressBar() {
  int pct = _bootPct;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  char fill[19];
  int n = pct * 18 / 100;
  for (int i = 0; i < 18; i++) fill[i] = (i < n) ? '#' : '-';
  fill[18] = '\0';
  char line[26];
  snprintf(line, sizeof(line), "[%s] %3d%%", fill, pct);
  clearRegion(0, 56, 127, 64);  // 清 y=56..63
  textEN(1, 56, line, &Font8);
}

void OledDisplay::bootProgress(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  _bootPct = pct;
  bootDrawProgressBar();
  refreshRows(56, 63);  // 物理页 0
}

void OledDisplay::bootDrawLogRow(int row) {
  if (row < 0 || row >= kBootRows) return;
  int y = 8 + row * 8;
  clearRegion(0, y, 127, y + 8);
  if (row < _bootCount && _bootLines[row][0])
    textEN(1, y, _bootLines[row], &Font8);
}

void OledDisplay::bootDrawLogs() {
  clearRegion(0, 8, 127, 56);  // 清 6 行日志区
  for (int i = 0; i < kBootRows; i++) {
    if (i < _bootCount && _bootLines[i][0])
      textEN(1, 8 + i * 8, _bootLines[i], &Font8);
  }
  refreshRows(8, 55);  // 逻辑页 1..6 -> 物理页 6..1
}

void OledDisplay::bootLog(const char* msg, int state) {
  const char* st = (state == 0) ? ".." : (state == 1 ? "OK" : "!!");
  char line[26];
  float t = (float)(millis() - _bootStartMs) / 1000.0f;
  // [ 0.0] OLED SSD1309 [OK]  = 24 字符,一行放得下
  snprintf(line, sizeof(line), "[%4.1f] %-12.12s [%s]", t,
           msg ? msg : "", st);
  if (_bootCount < kBootRows) {
    snprintf(_bootLines[_bootCount], sizeof(_bootLines[0]), "%s", line);
    _bootCount++;
  } else {
    memmove(_bootLines[0], _bootLines[1],
            sizeof(_bootLines[0]) * (kBootRows - 1));
    snprintf(_bootLines[kBootRows - 1], sizeof(_bootLines[0]), "%s", line);
  }
  bootDrawLogs();
}

void OledDisplay::bootUpdateLast(bool ok) {
  if (_bootCount <= 0) return;
  int idx = (_bootCount < kBootRows) ? (_bootCount - 1) : (kBootRows - 1);
  int len = (int)strlen(_bootLines[idx]);
  // 行尾是 "[..]":把中间两个点改成 OK 或 !!
  if (len >= 4) {
    _bootLines[idx][len - 3] = ok ? 'O' : '!';
    _bootLines[idx][len - 2] = ok ? 'K' : '!';
  }
  int y = 8 + idx * 8;
  bootDrawLogRow(idx);
  refreshRows(y, y + 7);  // 单页局部刷新
}

void OledDisplay::bootAnimateProgress(int from, int to, uint32_t ms) {
  if (from < 0) from = 0;
  if (to > 100) to = 100;
  if (ms == 0) {
    bootProgress(to);
    return;
  }
  uint32_t start = millis();
  int last = -1;
  while ((uint32_t)(millis() - start) < ms) {
    int pct = from + (int)((int32_t)(to - from) *
                           (int32_t)(millis() - start) / (int32_t)ms);
    if (pct != last) {
      bootProgress(pct);
      last = pct;
    }
    delay(40);
  }
  bootProgress(to);
}

void OledDisplay::bootDrawPrompt(bool cursorOn) {
  clearRegion(0, 56, 127, 64);
  const char* p = "root@si4703:~#";
  textEN(1, 56, p, &Font8);
  if (cursorOn) {
    int cx = 1 + (int)strlen(p) * 5 + 2;
    fillRect(cx, 56, cx + 4, 64, true);  // 5x8 方块光标
  }
}

void OledDisplay::bootFinish() {
  bootProgress(100);
  delay(150);
  // shell 提示符 + 闪烁方块光标,像 Linux 启动完停在登录/终端
  for (int i = 0; i < 6; i++) {
    bootDrawPrompt(i & 1);
    refreshRows(56, 63);
    delay(100);
  }
  bootDrawPrompt(true);
  refreshRows(56, 63);
  delay(80);
}

void OledDisplay::showMessage(const char* line1, const char* line2) {
  Paint_Clear(BLACK);
  if (line1) Paint_DrawString_EN(2, 8, line1, &Font16, WHITE, WHITE);
  if (line2) Paint_DrawString_EN(2, 32, line2, &Font12, WHITE, WHITE);
  flush();
}

void OledDisplay::showTestPattern() {
  Paint_Clear(BLACK);
  // 几何图形(与 Waveshare 例程第 1 页相同)
  Paint_DrawPoint(20, 10, WHITE, DOT_PIXEL_1X1, DOT_STYLE_DFT);
  Paint_DrawPoint(30, 10, WHITE, DOT_PIXEL_2X2, DOT_STYLE_DFT);
  Paint_DrawPoint(40, 10, WHITE, DOT_PIXEL_3X3, DOT_STYLE_DFT);
  Paint_DrawLine(10, 10, 10, 20, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(20, 20, 20, 30, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(30, 30, 30, 40, WHITE, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawCircle(60, 30, 15, WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawCircle(100, 40, 20, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawRectangle(50, 30, 60, 40, WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawString_EN(2, 52, "Si4703 FM TEST OK", &Font8, WHITE, WHITE);
  flush();
}
