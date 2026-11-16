// keys.cpp -- 见 keys.h
#include "keys.h"

void Keys::begin(const uint8_t pins[KEY_COUNT]) {
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    _pins[i] = pins[i];
    pinMode(_pins[i], INPUT_PULLUP);
  }
  _held = 0;
  _lastRaw = 0;
  _lastChangeMs = millis();
}

uint8_t Keys::update() {
  // 原始采样:低电平=按下
  uint8_t raw = 0;
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    if (digitalRead(_pins[i]) == LOW) raw |= (uint8_t)(1u << i);
  }
  if (raw != _lastRaw) {
    _lastRaw = raw;
    _lastChangeMs = millis();
    return 0;
  }
  // 电平稳定满消抖时间才采纳;只上报由“没按”变“按下”的边沿
  if ((uint32_t)(millis() - _lastChangeMs) < KEY_DEBOUNCE_MS) return 0;
  uint8_t pressed = (uint8_t)(raw & (uint8_t)~_held);
  _held = raw;
  return pressed;
}

const char* Keys::name(uint8_t mask) {
  switch (mask) {
    case KEY1_MASK: return "KEY1";
    case KEY2_MASK: return "KEY2";
    case KEY3_MASK: return "KEY3";
    case KEY4_MASK: return "KEY4";
    case KEY5_MASK: return "KEY5";
    default: return "KEY?";
  }
}
