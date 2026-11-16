// keys.h -- 5 按键模块(按下=低电平,内部上拉,20ms 软件消抖)
// 职责:只做“按下了哪个键”的边沿检测,不解释按键含义;
// 按键含义(音量/调频/搜台)在主程序里定,见 Si4703Radio.ino。
#pragma once
#include <Arduino.h>

#define KEY_COUNT 5

// 按键位掩码(与 keyNames()/pins 顺序一致)
#define KEY1_MASK 0x01
#define KEY2_MASK 0x02
#define KEY3_MASK 0x04
#define KEY4_MASK 0x08
#define KEY5_MASK 0x10

class Keys {
public:
  // pins 数组须按 KEY1..KEY5 顺序给 5 个引脚
  void begin(const uint8_t pins[KEY_COUNT]);
  // 每轮 loop 调一次;返回本轮新按下(消抖后下降沿)的键掩码,无按键返回 0
  uint8_t update();
  // 当前按住的键掩码(消抖后电平)
  uint8_t held() const { return _held; }
  static const char* name(uint8_t mask); // 单个掩码转 "KEY1" 之类

private:
  uint8_t _pins[KEY_COUNT] = {0, 0, 0, 0, 0};
  uint8_t _held = 0;              // 当前按住(消抖后)
  uint8_t _lastRaw = 0;           // 上次原始采样(1=按下)
  uint32_t _lastChangeMs = 0;     // 原始电平最后一次变化的时刻
};

#define KEY_DEBOUNCE_MS 20
