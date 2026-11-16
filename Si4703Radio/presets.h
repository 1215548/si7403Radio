// presets.h -- 预存台(自动搜台得到的电台表)
// 自动搜台:从 87.5MHz 起反复 seek up,记录每个停下的台,直到绕回起点、
// 满 40 个或超时(120s);每跳之间刷新进度并检查取消(KEY1)。
// 预存台存 NVS("radio"),上电恢复;首页 KEY3/KEY4 在预存台之间切换,
// 无预存时退回 ±0.1MHz 手动调谐。
#pragma once
#include <Arduino.h>
#include "radio_si4703.h"

struct Station {
  float freq;
  int rssi;
};

#define PRESETS_MAX 40

class Presets {
public:
  void begin();  // 从 NVS 读预存台
  int count() const { return _count; }
  Station get(int i) const;
  int current() const { return _current; }
  void setCurrent(int i) { _current = i; }
  // 全波段自动搜台;onProgress(已找到个数,当前频率);cancel() 返回 true 即停
  int scan(Si4703Radio& radio, void (*onProgress)(int, float),
           bool (*cancel)());
  bool next(Si4703Radio& radio);  // 切下一台,false=无预存
  bool prev(Si4703Radio& radio);  // 切上一台,false=无预存
  void tuneCurrent(Si4703Radio& radio);  // 调到当前预存台

private:
  void save();
  void clear();
  bool add(float freq, int rssi);  // 已存在(±0.05MHz)则跳过

  Station _list[PRESETS_MAX];
  int _count = 0;
  int _current = -1;
};
