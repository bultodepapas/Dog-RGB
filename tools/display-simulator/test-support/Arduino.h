#pragma once
#include <cassert>
#include <array>
#include <cstdint>
#include <string>
inline uint32_t now_ms = 10000, now_us = 10000000;
inline uint32_t millis() { return now_ms; }
inline uint32_t micros() { return now_us; }
constexpr int LOW = 0, HIGH = 1;
constexpr int INPUT_PULLUP = 2;
inline int button_level = HIGH;
inline void pinMode(int pin, int mode) { assert(pin == 0 && mode == INPUT_PULLUP); }
inline int digitalRead(int pin) { assert(pin == 0); return button_level; }
inline int light_level = LOW;
inline unsigned bitmap_calls = 0, bitmap_pixels = 0, begins = 0, text_calls = 0;
inline bool begin_result = true;
inline std::array<uint16_t, 240 * 280> panel_frame{};
inline void digitalWrite(int pin, int value) {
  assert(pin == 15);
  if (value) assert(bitmap_pixels >= 240 * 280);
  light_level = value;
}
class Print {
 public:
  std::string output;
  size_t write(const uint8_t *data, size_t size) {
    output.append(reinterpret_cast<const char *>(data), size); return size;
  }
};
class Console : public Print {
 public:
  std::string input;
  int available() { return static_cast<int>(input.size()); }
  int read() { int value = input.front(); input.erase(0, 1); return value; }
};
inline Console Serial;
