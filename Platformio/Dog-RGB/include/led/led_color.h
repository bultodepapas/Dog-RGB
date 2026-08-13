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

// Canonical logical/physical conversion shared by palette generation, power
// estimation and the NeoPixel transport. Palette RGBW entries are chosen so
// rgb_to_rgbw(rgbw_to_rgb(entry)) is lossless.
Rgbw rgb_to_rgbw(const Rgb &color);
Rgb rgbw_to_rgb(const Rgbw &color);
Rgbw scale_rgbw(const Rgbw &color, uint8_t scale);
Rgb blend_rgb(const Rgb &from, const Rgb &to, uint8_t amount);
Rgbw blend_rgbw(const Rgbw &from, const Rgbw &to, uint8_t amount);

} // namespace led
