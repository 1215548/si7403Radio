// led.cpp -- 见 led.h
#include "led.h"
#include "pins.h"

void Led::writeOut(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(_pin, on ? LOW : HIGH);
#else
  digitalWrite(_pin, on ? HIGH : LOW);
#endif
}

void Led::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, OUTPUT);
  off();
}

void Led::on() {
  _blinking = false;
  _errorBlink = false;
  _steady = true;
  writeOut(true);
}

void Led::off() {
  _blinking = false;
  _errorBlink = false;
  _steady = false;
  writeOut(false);
}

void Led::set(bool on) {
  if (on) this->on(); else this->off();
}

void Led::toggle() { set(!_steady); }

void Led::blink(uint16_t ms) {
  _errorBlink = false;
  _blinking = true;
  _untilMs = millis() + ms;
  writeOut(true);
}

void Led::blinkError() {
  _blinking = false;
  _errorBlink = true;
  _blinkLevel = true;
  _nextToggleMs = millis() + 250;
  writeOut(true);
}

void Led::update() {
  if (_blinking) {
    if ((int32_t)(millis() - _untilMs) >= 0) {
      _blinking = false;
      writeOut(_steady);
    }
  } else if (_errorBlink) {
    if ((int32_t)(millis() - _nextToggleMs) >= 0) {
      _blinkLevel = !_blinkLevel;
      writeOut(_blinkLevel);
      _nextToggleMs = millis() + 250;
    }
  }
}
