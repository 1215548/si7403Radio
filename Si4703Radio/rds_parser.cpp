// rds_parser.cpp -- 见 rds_parser.h
#include "rds_parser.h"
#include <string.h>

// PTY 节目类型(IEC 62106),短名上屏用
static const char kPtyNames[32][9] = {
    "None",   "News",   "Affairs", "Info",   "Sport",  "Educat.",
    "Drama",  "Culture", "Science", "Varied", "Pop Mus", "Rock",
    "EasyLis", "LightCl", "Serious", "Oth Mus", "Weather", "Finance",
    "Children", "Social", "Religion", "PhoneIn", "Travel", "Leisure",
    "Jazz",   "Country", "Nation",  "Oldies", "Folk",   "Documen",
    "Alarm T", "Alarm",
};

const char* RdsParser::ptyName(uint8_t pty) {
  return (pty < 32) ? kPtyNames[pty] : "None";
}

char RdsParser::sanitize(uint8_t raw) {
  if (raw < 0x20 || raw > 0x7E) return ' ';
  return (char)raw;
}

void RdsParser::reset() {
  memset(&_info, 0, sizeof(_info));
  memset(_info.ps, ' ', 8);
  _info.ps[8] = '\0';
  _info.rt[0] = '\0';
  for (uint8_t i = 0; i < 4; i++) _segSeen[i] = false;
  _rtAb = false;
  _rtTerminated = false;
  _rtTermPos = 0;
}

void RdsParser::clearRt() {
  memset(_info.rt, 0, sizeof(_info.rt));
  _info.rtLen = 0;
  _info.rtComplete = false;
  _rtTerminated = false;
  _rtTermPos = 0;
}

void RdsParser::clearTexts() {
  memset(_info.ps, ' ', 8);
  _info.ps[8] = '\0';
  _info.psComplete = false;
  for (uint8_t i = 0; i < 4; i++) _segSeen[i] = false;
  clearRt();
  _info.hasCt = false;
}

bool RdsParser::setSynced(bool synced) {
  if (synced == _info.synced) return false;
  _info.synced = synced;
  return true;
}

void RdsParser::putRtChar(uint8_t idx, uint8_t raw, bool& changed) {
  if (idx >= 64) return;
  // 已见结束符:之后的位置全是填充,不再收
  if (_rtTerminated && idx >= _rtTermPos) return;
  if (raw == 0x0D) {  // 文本结束符
    if (!_rtTerminated || idx < _rtTermPos) {
      _rtTerminated = true;
      _rtTermPos = idx;
      if (idx < _info.rtLen) _info.rtLen = idx;
      _info.rt[_info.rtLen] = '\0';
      _info.rtComplete = true;
      changed = true;
    }
    return;
  }
  char ch = sanitize(raw);
  if (_info.rt[idx] != ch) {
    _info.rt[idx] = ch;
    changed = true;
  }
  if (idx >= _info.rtLen) _info.rtLen = (uint8_t)(idx + 1);
  // 去尾空格:电台常拿空格填充,尾空格不算正文
  while (_info.rtLen > 0 && _info.rt[_info.rtLen - 1] == ' ') _info.rtLen--;
  _info.rt[_info.rtLen] = '\0';
  if (_info.rtLen >= 64) _info.rtComplete = true;
}

// MJD -> 年月日(Fliegel-Van Flandern 算法)。
// MJD 从午夜起算,同日中午的儒略日整数 = MJD + 2400001。
void RdsParser::mjdToDate(uint32_t mjd, int& y, int& m, int& d) {
  long jd = (long)mjd + 2400001L;
  long l = jd + 68569;
  long n = (4 * l) / 146097;
  l = l - (146097 * n + 3) / 4;
  long i = (4000 * (l + 1)) / 1461001;
  l = l - (1461 * i) / 4 + 31;
  long j = (80 * l) / 2447;
  d = (int)(l - (2447 * j) / 80);
  l = j / 11;
  m = (int)(j + 2 - 12 * l);
  y = (int)(100 * (n - 49) + i + l);
}

bool RdsParser::feed(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                     uint8_t blerA, uint8_t blerB, uint8_t blerC,
                     uint8_t blerD, bool synced) {
  bool changed = setSynced(synced);
  // 误码门:A/B 块不可信则整组丢弃(组类型/PI 都读不准)
  if (blerA > 1 || blerB > 1) {
    _info.badGroups++;
    return changed;
  }
  // PI 变了=换了个台,旧台的台名/文本/时间全部作废
  if (a != _info.pi) {
    if (_info.pi != 0) {
      clearTexts();
      changed = true;
    }
    _info.pi = a;
    changed = true;
  }
  uint8_t group = (uint8_t)((b >> 12) & 0xF);
  uint8_t version = (uint8_t)((b >> 11) & 1);
  bool tp = ((b >> 10) & 1) != 0;
  uint8_t pty = (uint8_t)((b >> 5) & 0x1F);
  if (tp != _info.tp) {
    _info.tp = tp;
    changed = true;
  }
  if (pty != _info.pty) {
    _info.pty = pty;
    changed = true;
  }
  _info.groups++;

  switch (group) {
    case 0: {  // PS 台名 + TA
      bool ta = ((b >> 4) & 1) != 0;
      if (ta != _info.ta) {
        _info.ta = ta;
        changed = true;
      }
      if (blerD > 1) break;  // 台名字符在 Block D
      uint8_t seg = (uint8_t)(b & 0x3);
      char ch1 = sanitize((uint8_t)(d >> 8));
      char ch2 = sanitize((uint8_t)(d & 0xFF));
      if (_info.ps[seg * 2] != ch1) {
        _info.ps[seg * 2] = ch1;
        changed = true;
      }
      if (_info.ps[seg * 2 + 1] != ch2) {
        _info.ps[seg * 2 + 1] = ch2;
        changed = true;
      }
      _segSeen[seg] = true;
      bool complete =
          _segSeen[0] && _segSeen[1] && _segSeen[2] && _segSeen[3];
      if (complete != _info.psComplete) {
        _info.psComplete = complete;
        changed = true;
      }
      break;
    }
    case 2: {  // RadioText
      bool ab = ((b >> 4) & 1) != 0;
      if (ab != _rtAb) {  // A/B 翻转=新文本,旧的清掉
        _rtAb = ab;
        clearRt();
        changed = true;
      }
      if (version == 0) {  // 2A:64 字符,每组 4 个
        uint8_t pos = (uint8_t)(b & 0xF);  // 0~15
        if (blerC <= 1) {
          putRtChar((uint8_t)(pos * 4), (uint8_t)(c >> 8), changed);
          putRtChar((uint8_t)(pos * 4 + 1), (uint8_t)(c & 0xFF), changed);
        }
        if (blerD <= 1) {
          putRtChar((uint8_t)(pos * 4 + 2), (uint8_t)(d >> 8), changed);
          putRtChar((uint8_t)(pos * 4 + 3), (uint8_t)(d & 0xFF), changed);
        }
      } else {  // 2B:32 字符,每组 2 个(Block D)
        if (blerD > 1) break;
        uint8_t pos = (uint8_t)(b & 0xF);  // 0~7
        putRtChar((uint8_t)(pos * 2), (uint8_t)(d >> 8), changed);
        putRtChar((uint8_t)(pos * 2 + 1), (uint8_t)(d & 0xFF), changed);
      }
      break;
    }
    case 4: {  // CT 时钟(只认 A 版)
      if (version != 0) break;
      if (blerC > 1 || blerD > 1) break;
      uint32_t mjd = ((uint32_t)(b & 0x1) << 16) | c;
      // MJD 合理范围约 2009~2050 年,之外当噪声组丢弃
      if (mjd < 55000 || mjd > 70000) break;
      int hour = (d >> 11) & 0x1F;
      int minute = (d >> 5) & 0x3F;
      if (hour > 23 || minute > 59) break;
      int offMin = (((d >> 4) & 0x1) ? -(d & 0xF) : (d & 0xF)) * 30;
      int totalMin = hour * 60 + minute + offMin;
      int dayShift = 0;
      if (totalMin < 0) {
        totalMin += 1440;
        dayShift = -1;
      } else if (totalMin >= 1440) {
        totalMin -= 1440;
        dayShift = 1;
      }
      int y, m, dd;
      mjdToDate(mjd + (uint32_t)dayShift, y, m, dd);
      int hh = totalMin / 60;
      int mm = totalMin % 60;
      if (!_info.hasCt || _info.ctYear != y || _info.ctMonth != m ||
          _info.ctDay != dd || _info.ctHour != hh || _info.ctMinute != mm) {
        _info.ctYear = y;
        _info.ctMonth = m;
        _info.ctDay = dd;
        _info.ctHour = hh;
        _info.ctMinute = mm;
        _info.hasCt = true;
        changed = true;
      }
      break;
    }
    default:
      break;  // 1/3/5/14/15 组:PIN/ODA/TDC/EON,本工程不显示
  }
  return changed;
}
