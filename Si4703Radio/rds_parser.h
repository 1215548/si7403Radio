// rds_parser.h -- RDS 组解析(纯逻辑,不依赖 Si4703 驱动)
//
// Si4703 只交原始 RDS 组:每收到一组,芯片把 4 个 16 位块
// (RDSA/RDSB/RDSC/RDSD)放在 0Ch~0Fh,详见 radio_si4703.cpp 的
// pollRds()。本模块把组按 RDS 标准(IEC 62106)拼成能显示的东西:
//   Block A = PI 码(电台唯一识别);Block B 高 4 位=组类型,
//   bit11=版本 A/B,bit10=TP,bits9:5=PTY。
//   0 组:PS 台名(8 字符,每组 2 个,段号在 Block B bits1:0);
//        TA 在 Block B bit4;AF 在 Block C(本模块不显示)。
//   2 组:RadioText,A 版每组 4 字符(位置 Block B bits3:0,0~15,
//        共 64 字符),B 版每组 2 字符(共 32 字符);Block B bit4
//        是 A/B 切换标志,翻转即新文本;0x0D 是文本结束符。
//   4A 组:CT 时钟,Block C=修正儒略日 MJD,Block D=时/分/时区偏移。
// 字符集:RDS 是拉丁字符,0x20~0x7E 直通 ASCII,其余换空格;
// 中文电台名发不出来,个别台用 GB 编码塞 RT 会显示乱码,属台端问题。
// 误码门:调用方按 verbose 模式传各块 BLER(0=无错,1=1~2 个可纠错,
// 2=3~5 个,3=6+ 或不可纠);BLER>1 的块不可信,A/B 块不可信则整组丢弃。
#pragma once
#include <Arduino.h>

// 解析结果,显示与串口共用
struct RdsInfo {
  char ps[9];      // 台名,8 字符(未收全的段留空格)
  bool psComplete; // 4 个段都收过至少一次
  char rt[65];     // RadioText,按 rtLen 截断
  uint8_t rtLen;   // 有效长度(已去尾空格/截断到 0x0D)
  bool rtComplete; // 收到 0x0D 结束符或收满 64 字符
  uint16_t pi;     // PI 码
  uint8_t pty;     // 节目类型 0~31
  bool tp;         // Traffic Programme(本台播路况节目)
  bool ta;         // Traffic Announcement(正在播路况)
  bool hasCt;      // 收到过 CT 时间
  int ctYear, ctMonth, ctDay, ctHour, ctMinute;  // CT 本地时间
  bool synced;     // 解码器已同步(RDSS)
  uint32_t groups;    // 接受的组数
  uint32_t badGroups; // 因 BLER 丢弃的组数
};

class RdsParser {
public:
  void reset();  // 换台/重上电时清掉旧台的全部内容
  // 喂一组,任一可见字段变化返回 true;synced 由调用方传 RDSS 状态。
  bool feed(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t blerA,
            uint8_t blerB, uint8_t blerC, uint8_t blerD, bool synced);
  // 只更新同步状态(无新组时也可能变),变了返回 true
  bool setSynced(bool synced);
  const RdsInfo& info() const { return _info; }
  // PTY 代码 0~31 的短名(≤8 字符,上屏用)
  static const char* ptyName(uint8_t pty);

private:
  static char sanitize(uint8_t raw);  // RDS 字符 -> 可显示 ASCII
  void clearTexts();                  // PI 变了:台名/文本/时间作废
  void clearRt();
  void putRtChar(uint8_t idx, uint8_t raw, bool& changed);
  static void mjdToDate(uint32_t mjd, int& y, int& m, int& d);

  RdsInfo _info;
  bool _segSeen[4] = {false, false, false, false};
  bool _rtAb = false;        // 当前 RT 文本的 A/B 标志
  bool _rtTerminated = false;  // 已见 0x0D 结束符
  uint8_t _rtTermPos = 0;    // 结束符位置,之后的字符是填充
};
