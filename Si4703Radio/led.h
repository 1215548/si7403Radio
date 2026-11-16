// led.h -- 指示灯模块(1 个 LED,高电平亮)
// 职责:开关、非阻塞闪烁(按键反馈/故障提示)。update() 须在 loop 里常调。
#pragma once
#include <Arduino.h>

class Led {
public:
  void begin(uint8_t pin);
  void on();                 // 常亮
  void off();                // 常灭
  void set(bool on);         // 常亮/常灭
  void toggle();
  bool isOn() const { return _steady; }
  // 闪一次 ms 毫秒后恢复原来的常亮/常灭状态(非阻塞,靠 update() 计时)
  void blink(uint16_t ms = 120);
  // 故障慢闪:亮 250ms / 灭 250ms 循环,调 on()/off() 可退出
  void blinkError();
  void update();

private:
  void writeOut(bool on);  // 按 LED_ACTIVE_LOW 把“亮/灭”翻成实际电平
  uint8_t _pin = 0;
  bool _steady = false;      // 常亮状态(闪烁结束后恢复到它)
  bool _blinking = false;    // 正在单次闪烁
  bool _errorBlink = false;  // 正在故障慢闪
  uint32_t _untilMs = 0;     // 单次闪烁结束时刻
  uint32_t _nextToggleMs = 0;
  bool _blinkLevel = false;
};
