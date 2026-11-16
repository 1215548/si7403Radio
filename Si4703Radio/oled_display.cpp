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

bool OledDisplay::begin() {
  System_Init();
  OLED_2IN42_Init();
  Driver_Delay_ms(500);
  OLED_2IN42_Clear();
  Paint_NewImage(s_frame, OLED_2IN42_WIDTH, OLED_2IN42_HEIGHT, 270, BLACK);
  Paint_SelectImage(s_frame);
  Paint_Clear(BLACK);
  showMessage("Si4703 FM", "OLED OK");
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
}

void OledDisplay::clearRegion(int x0, int y0, int x1, int y1) {
  Paint_DrawRectangle((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1, BLACK,
                      DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void OledDisplay::refreshPages(uint8_t page0, uint8_t page1) {
  OLED_2IN42_RefreshPages(s_frame, page0, page1);
  // 同步哈希:显示内容与显存一致,下次 flush() 仍可正确跳过
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

void OledDisplay::rect(int x0, int y0, int x1, int y1) {
  Paint_DrawRectangle((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1, WHITE,
                      DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
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
