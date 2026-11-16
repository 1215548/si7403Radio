// presets.cpp -- 见 presets.h
#include "presets.h"
#include <Preferences.h>

static Preferences s_nvs;

void Presets::begin() {
  s_nvs.begin("radio", false);
  _count = s_nvs.getInt("count", 0);
  if (_count < 0 || _count > PRESETS_MAX) _count = 0;
  for (int i = 0; i < _count; i++) {
    char k[8];
    snprintf(k, sizeof(k), "f%d", i);
    _list[i].freq = s_nvs.getFloat(k, 87.5f);
    snprintf(k, sizeof(k), "r%d", i);
    _list[i].rssi = s_nvs.getInt(k, 0);
  }
  _current = (_count > 0) ? 0 : -1;
}

Station Presets::get(int i) const {
  if (i < 0 || i >= _count) return {87.5f, 0};
  return _list[i];
}

void Presets::save() {
  s_nvs.putInt("count", _count);
  for (int i = 0; i < _count; i++) {
    char k[8];
    snprintf(k, sizeof(k), "f%d", i);
    s_nvs.putFloat(k, _list[i].freq);
    snprintf(k, sizeof(k), "r%d", i);
    s_nvs.putInt(k, _list[i].rssi);
  }
}

void Presets::clear() {
  _count = 0;
  _current = -1;
}

bool Presets::add(float freq, int rssi) {
  for (int i = 0; i < _count; i++) {
    if (fabsf(_list[i].freq - freq) < 0.05f) return false;
  }
  if (_count >= PRESETS_MAX) return false;
  _list[_count++] = {freq, rssi};
  return true;
}

int Presets::scan(Si4703Radio& radio, void (*onProgress)(int, float),
                  bool (*cancel)()) {
  clear();
  uint32_t t0 = millis();
  radio.tune(87.5f);
  float first = -1;
  for (int hop = 0; hop < PRESETS_MAX + 2; hop++) {
    if (millis() - t0 > 120000) break;  // 总超时 120s
    if (cancel && cancel()) break;
    if (!radio.seek(true)) break;  // 一整圈没找到台
    float f = radio.freqMHz();
    if (first < 0) {
      first = f;
    } else if (f <= first + 0.05f) {
      break;  // 绕回起点,扫完一圈
    }
    if (add(f, radio.rssi()) && onProgress) onProgress(_count, f);
    if (_count >= PRESETS_MAX) break;
  }
  if (_count > 0) {
    _current = 0;
    tuneCurrent(radio);
  }
  save();
  return _count;
}

void Presets::tuneCurrent(Si4703Radio& radio) {
  if (_current < 0 || _current >= _count) return;
  radio.tune(_list[_current].freq);
}

bool Presets::next(Si4703Radio& radio) {
  if (_count == 0) return false;
  _current = (_current + 1) % _count;
  tuneCurrent(radio);
  return true;
}

bool Presets::prev(Si4703Radio& radio) {
  if (_count == 0) return false;
  _current = (_current - 1 + _count) % _count;
  tuneCurrent(radio);
  return true;
}
