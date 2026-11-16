// cn_font.cpp -- 见 cn_font.h
#include "cn_font.h"
#include "gb2312_font.h"
#include "GUI_Paint.h"
#include "fonts.h"

const uint8_t* CnFont_findGlyph(uint16_t unicode) {
  // kGb2312Unicode 按 Unicode 升序,二分查找
  int lo = 0, hi = GB2312_FONT_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = kGb2312Unicode[mid];
    if (v == unicode) return kGb2312Glyphs[mid];
    if (v < unicode) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

void CnFont_draw16(int x, int y, uint16_t unicode, bool invert) {
  const uint8_t* g = CnFont_findGlyph(unicode);
  UWORD color = invert ? BLACK : WHITE;
  if (!g) {
    // 查无此字:画空心框占位,不静默丢字
    Paint_DrawRectangle((UWORD)x, (UWORD)y, (UWORD)(x + 15), (UWORD)(y + 15),
                        color, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    return;
  }
  for (int r = 0; r < 16; r++) {
    for (int c = 0; c < 16; c++) {
      if (g[r * 2 + c / 8] & (0x80 >> (c % 8))) {
        Paint_DrawPoint((UWORD)(x + c), (UWORD)(y + r), color, DOT_PIXEL_1X1,
                        DOT_STYLE_DFT);
      }
    }
  }
}

// 取 UTF-8 下一个字符的码点,返回下一字符指针;4 字节字符返回 0xFFFD
static const char* utf8Next(const char* p, uint16_t& cp) {
  uint8_t c = (uint8_t)*p;
  if (c < 0x80) {
    cp = c;
    return p + 1;
  }
  if ((c & 0xE0) == 0xC0 && p[1]) {
    cp = (uint16_t)(((c & 0x1F) << 6) | ((uint8_t)p[1] & 0x3F));
    return p + 2;
  }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2]) {
    cp = (uint16_t)(((c & 0x0F) << 12) | (((uint8_t)p[1] & 0x3F) << 6) |
                    ((uint8_t)p[2] & 0x3F));
    return p + 3;
  }
  cp = 0xFFFD;
  return p + 1;
}

int CnFont_drawMixed(int x, int y, const char* utf8, bool invert) {
  const char* p = utf8;
  while (*p) {
    uint16_t cp;
    const char* next = utf8Next(p, cp);
    if (cp < 0x80) {
      char one[2] = {(char)cp, '\0'};
      Paint_DrawString_EN((UWORD)x, (UWORD)y, one, &Font16,
                          invert ? BLACK : WHITE, invert ? WHITE : BLACK);
      x += 11;  // Font16 宽 11px
    } else {
      CnFont_draw16(x, y, cp, invert);
      x += 16;
    }
    p = next;
  }
  return x;
}

// 16x16 字形降采样成 12x12:每个输出像素对应源图 2x2 区块,
// 4 个源像素里至少 2 个点亮才输出。等比缩小时笔画会略粗,
// 但 12px 下比直接丢行丢列更不容易断笔(宿主仿真里对比过)。
void CnFont_draw12(int x, int y, uint16_t unicode, bool invert) {
  const uint8_t* g = CnFont_findGlyph(unicode);
  UWORD color = invert ? BLACK : WHITE;
  if (!g) {
    Paint_DrawRectangle((UWORD)x, (UWORD)y, (UWORD)(x + 11), (UWORD)(y + 11),
                        color, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    return;
  }
  for (int oy = 0; oy < 12; oy++) {
    int sy0 = oy * 16 / 12;
    int sy1 = ((oy + 1) * 16 + 11) / 12;  // 向上取整,保证覆盖到尾
    for (int ox = 0; ox < 12; ox++) {
      int sx0 = ox * 16 / 12;
      int sx1 = ((ox + 1) * 16 + 11) / 12;
      int on = 0;
      for (int sy = sy0; sy < sy1; sy++) {
        for (int sx = sx0; sx < sx1; sx++) {
          if (g[sy * 2 + sx / 8] & (0x80 >> (sx % 8))) on++;
        }
      }
      if (on >= 2) Paint_SetPixel((UWORD)(x + ox), (UWORD)(y + oy), color);
    }
  }
}

int CnFont_drawMixed12(int x, int y, const char* utf8, bool invert) {
  const char* p = utf8;
  while (*p) {
    uint16_t cp;
    const char* next = utf8Next(p, cp);
    if (cp < 0x80) {
      char one[2] = {(char)cp, '\0'};
      // Font8 高 8px,在 12px 行内垂直居中
      Paint_DrawString_EN((UWORD)x, (UWORD)(y + 2), one, &Font8,
                          invert ? BLACK : WHITE, invert ? WHITE : BLACK);
      x += 5;
    } else {
      CnFont_draw12(x, y, cp, invert);
      x += 12;
    }
    p = next;
  }
  return x;
}

int CnFont_mixedWidth12(const char* utf8) {
  int w = 0;
  const char* p = utf8;
  while (*p) {
    uint16_t cp;
    p = utf8Next(p, cp);
    w += (cp < 0x80) ? 5 : 12;
  }
  return w;
}

void CnFont_truncate12(const char* utf8, int maxWidth, char* out,
                       size_t outSize) {
  int w = 0;
  size_t n = 0;
  const char* p = utf8;
  while (*p && n + 1 < outSize) {
    uint16_t cp;
    const char* next = utf8Next(p, cp);
    int cw = (cp < 0x80) ? 5 : 12;
    if (w + cw > maxWidth) break;
    size_t len = (size_t)(next - p);
    if (n + len >= outSize) break;
    memcpy(out + n, p, len);
    n += len;
    w += cw;
    p = next;
  }
  out[n] = '\0';
}

int CnFont_mixedWidth(const char* utf8) {
  int w = 0;
  const char* p = utf8;
  while (*p) {
    uint16_t cp;
    p = utf8Next(p, cp);
    w += (cp < 0x80) ? 11 : 16;
  }
  return w;
}

void CnFont_truncate(const char* utf8, int maxWidth, char* out, size_t outSize) {
  int w = 0;
  size_t n = 0;
  const char* p = utf8;
  while (*p && n + 1 < outSize) {
    uint16_t cp;
    const char* next = utf8Next(p, cp);
    int cw = (cp < 0x80) ? 11 : 16;
    if (w + cw > maxWidth) break;
    size_t len = (size_t)(next - p);
    if (n + len >= outSize) break;
    memcpy(out + n, p, len);
    n += len;
    w += cw;
    p = next;
  }
  out[n] = '\0';
}
