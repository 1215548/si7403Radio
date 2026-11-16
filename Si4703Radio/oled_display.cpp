// oled_display.cpp -- 见 oled_display.h
#include "oled_display.h"
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

void OledDisplay::flush() { OLED_2IN42_Display(s_frame); }

void OledDisplay::showRadio(float freqMHz, int rssi, bool stereo, int volume,
                            bool muted, bool radioOk, const char* msg) {
  char buf[32];
  Paint_Clear(BLACK);
  // 第 1 行:大字体频率 "98.00" + "MHz"(100MHz 以上 6 位数,MHz 跟在后面)
  snprintf(buf, sizeof(buf), "%.2f", freqMHz);
  Paint_DrawString_EN(2, 2, buf, &Font24, WHITE, WHITE);
  Paint_DrawString_EN(2 + (UWORD)strlen(buf) * 17 + 2, 10, "MHz", &Font12,
                      WHITE, WHITE);
  // 分隔线
  Paint_DrawLine(0, 28, 127, 28, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  // 第 2 行:状态
  if (!radioOk) {
    Paint_DrawString_EN(2, 32, "NO RADIO", &Font12, WHITE, WHITE);
  } else if (muted) {
    snprintf(buf, sizeof(buf), "MUTE  VOL %d", volume);
    Paint_DrawString_EN(2, 32, buf, &Font12, WHITE, WHITE);
  } else {
    snprintf(buf, sizeof(buf), "RSSI %d %s VOL %d", rssi,
             stereo ? "ST" : "MO", volume);
    Paint_DrawString_EN(2, 32, buf, &Font12, WHITE, WHITE);
  }
  // 第 3 行:动态消息(上次按键/搜台结果,无消息时显示波段)
  if (msg && msg[0]) {
    Paint_DrawString_EN(2, 46, msg, &Font8, WHITE, WHITE);
  } else {
    Paint_DrawString_EN(2, 46, "FM 87.5-108MHz 100kHz", &Font8, WHITE, WHITE);
  }
  // 第 4 行:按键提示
  Paint_DrawString_EN(2, 56, "K1V- K2V+ K3F- K4F+ K5SK", &Font8, WHITE, WHITE);
  flush();
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
