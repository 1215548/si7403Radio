// cn_font.h -- 16x16 GB2312 点阵字 + UTF-8 混排绘制
// 字库:gb2312_font.h(7445 字,Noto Sans CJK SC 16px 渲染,全 GB2312)。
// 混排:中文用 16x16 点阵,ASCII 用 Font16(同高 16px),一行 16px。
// 另有 12px 紧凑版:16x16 字形按 2x2 覆盖降采样成 12x12,ASCII 用
// Font8(8px,垂直居中),用于菜单/信息页,一屏能多放一行。
// 绘制走 GUI_Paint 的逻辑坐标(与 Paint_DrawString_EN 一致)。
#pragma once
#include <Arduino.h>

// 在字库里找 Unicode 对应字形(32 字节),查无返回 nullptr
const uint8_t* CnFont_findGlyph(uint16_t unicode);
// 在 (x,y) 画一个汉字(16x16);invert=true 时画黑字(用于反白行)
void CnFont_draw16(int x, int y, uint16_t unicode, bool invert);
// 混排绘制一行 UTF-8 字符串,中文 16 点阵 + ASCII Font16,返回绘制后的 x
int CnFont_drawMixed(int x, int y, const char* utf8, bool invert);
// 混排字符串的像素宽度(中文 16px,ASCII 11px,空格 11px)
int CnFont_mixedWidth(const char* utf8);
// 把 UTF-8 字符串按像素宽度截断(超出部分丢弃),写入 out
void CnFont_truncate(const char* utf8, int maxWidth, char* out, size_t outSize);

// ---- 12px 紧凑版(菜单/信息页) ----
// 汉字 12x12(16x16 降采样),ASCII Font8 垂直居中,一行 12px
void CnFont_draw12(int x, int y, uint16_t unicode, bool invert);
int CnFont_drawMixed12(int x, int y, const char* utf8, bool invert);
int CnFont_mixedWidth12(const char* utf8);  // 中文 12px,ASCII 5px
void CnFont_truncate12(const char* utf8, int maxWidth, char* out,
                       size_t outSize);
