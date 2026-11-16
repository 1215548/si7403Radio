// led.cpp -- 见 led.h
#include "led.h"

void Led::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, OUTPUT);
  off();
}

void Led::on() {
  _blinking = false;
  _errorBlink = false;
  _steady = true;
  digitalWrite(_pin, HIGH);
}

void Led::off() {
  _blinking = false;
  _errorBlink = false;
  _steady = false;
  digitalWrite(_pin, LOW);
}

void Led::set(bool on) {
  if (on) this->on(); else this->off();
}

void Led::toggle() { set(!_steady); }

void Led::blink(uint16_t ms) {
  _errorBlink = false;
  _blinking = true;
  _untilMs = millis() + ms;
  digitalWrite(_pin, HIGH);
}

void Led::blinkError() {
  _blinking = false;
  _errorBlink = true;
  _blinkLevel = true;
  _nextToggleMs = millis() + 250;
  digitalWrite(_pin, HIGH);
}

void Led::update() {
  if (_blinking) {
    if ((int32_t)(millis() - _untilMs) >= 0) {
      _blinking = false;
      digitalWrite(_pin, _steady ? HIGH : LOW);
    }
  } else if (_errorBlink) {
    if ((int32_t)(millis() - _nextToggleMs) >= 0) {
      _blinkLevel = !_blinkLevel;
      digitalWrite(_pin, _blinkLevel ? HIGH : LOW);
      _nextToggleMs = millis() + 250;
    }
  }
}
