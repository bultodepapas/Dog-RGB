#include "led/led_color.h"

namespace led {
namespace {

static uint8_t saturating_add(uint8_t left, uint8_t right) {
  const uint16_t sum = static_cast<uint16_t>(left) + right;
  return sum > 255U ? 255U : static_cast<uint8_t>(sum);
}

static uint8_t blend_channel(uint8_t from, uint8_t to, uint8_t amount) {
  const uint16_t inverse = static_cast<uint16_t>(255U - amount);
  const uint32_t weighted = static_cast<uint32_t>(from) * inverse +
                            static_cast<uint32_t>(to) * amount + 127U;
  return static_cast<uint8_t>(weighted / 255U);
}

} // namespace

Rgbw rgb_to_rgbw(const Rgb &color) {
  uint8_t white = color.r < color.g ? color.r : color.g;
  if (color.b < white) {
    white = color.b;
  }
  return Rgbw{static_cast<uint8_t>(color.r - white),
              static_cast<uint8_t>(color.g - white),
              static_cast<uint8_t>(color.b - white), white};
}

Rgb rgbw_to_rgb(const Rgbw &color) {
  return Rgb{saturating_add(color.r, color.w),
             saturating_add(color.g, color.w),
             saturating_add(color.b, color.w)};
}

Rgbw scale_rgbw(const Rgbw &color, uint8_t scale) {
  if (scale == 255U) {
    return color;
  }
  return Rgbw{
      static_cast<uint8_t>((static_cast<uint16_t>(color.r) * scale) / 255U),
      static_cast<uint8_t>((static_cast<uint16_t>(color.g) * scale) / 255U),
      static_cast<uint8_t>((static_cast<uint16_t>(color.b) * scale) / 255U),
      static_cast<uint8_t>((static_cast<uint16_t>(color.w) * scale) / 255U)};
}

Rgb blend_rgb(const Rgb &from, const Rgb &to, uint8_t amount) {
  return Rgb{blend_channel(from.r, to.r, amount),
             blend_channel(from.g, to.g, amount),
             blend_channel(from.b, to.b, amount)};
}

Rgbw blend_rgbw(const Rgbw &from, const Rgbw &to, uint8_t amount) {
  return Rgbw{blend_channel(from.r, to.r, amount),
              blend_channel(from.g, to.g, amount),
              blend_channel(from.b, to.b, amount),
              blend_channel(from.w, to.w, amount)};
}

} // namespace led
