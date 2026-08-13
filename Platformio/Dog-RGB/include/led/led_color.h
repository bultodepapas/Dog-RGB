#pragma once

#include <stdint.h>

namespace led {

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct Rgbw {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;
};

} // namespace led
