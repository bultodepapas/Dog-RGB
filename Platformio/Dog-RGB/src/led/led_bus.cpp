#include "led/led_bus.h"

namespace led {

Rgbw rgb_to_rgbw(const Rgb &color) {
  uint8_t white = color.r < color.g ? color.r : color.g;
  if (color.b < white) {
    white = color.b;
  }
  return Rgbw{static_cast<uint8_t>(color.r - white),
              static_cast<uint8_t>(color.g - white),
              static_cast<uint8_t>(color.b - white), white};
}

Rgbw scale_rgbw(const Rgbw &color, uint8_t scale) {
  if (scale == 255) {
    return color;
  }
  return Rgbw{
      static_cast<uint8_t>((static_cast<uint16_t>(color.r) * scale) / 255U),
      static_cast<uint8_t>((static_cast<uint16_t>(color.g) * scale) / 255U),
      static_cast<uint8_t>((static_cast<uint16_t>(color.b) * scale) / 255U),
      static_cast<uint8_t>((static_cast<uint16_t>(color.w) * scale) / 255U)};
}

LedBus::LedBus(uint16_t pixel_count, int16_t pin_a, int16_t pin_b,
               bool dual_bus)
    : pixel_count_(pixel_count),
      dual_bus_(dual_bus),
      brightness_(255),
      strip_a_(pixel_count, pin_a, NEO_GRBW + NEO_KHZ800),
      strip_b_(pixel_count, pin_b, NEO_GRBW + NEO_KHZ800) {}

void LedBus::begin(uint8_t brightness) {
  brightness_ = brightness;
  strip_a_.begin();
  strip_a_.setBrightness(brightness_);
  strip_a_.clear();
  strip_a_.show();
  if (dual_bus_) {
    strip_b_.begin();
    strip_b_.setBrightness(brightness_);
    strip_b_.clear();
    strip_b_.show();
  }
}

void LedBus::set_brightness(uint8_t brightness) {
  brightness_ = brightness;
  strip_a_.setBrightness(brightness_);
  if (dual_bus_) {
    strip_b_.setBrightness(brightness_);
  }
}

void LedBus::configure_power(const PowerLimitConfig &config) {
  limiter_.configure(config);
}

void LedBus::write_strip(Adafruit_NeoPixel &strip, const Rgb *pixels,
                         uint8_t scale) {
  const uint16_t count = pixel_count_ < LED_STRIP_COUNT
                             ? pixel_count_
                             : LED_STRIP_COUNT;
  for (uint16_t i = 0; i < count; ++i) {
    const Rgbw color = scale_rgbw(rgb_to_rgbw(pixels[i]), scale);
    strip.setPixelColor(i, color.r, color.g, color.b, color.w);
  }
  strip.show();
}

void LedBus::show(const LedFrame &frame) {
  const PowerLimitDecision decision =
      limiter_.evaluate(frame, brightness_, dual_bus_ ? 2 : 1);
  write_strip(strip_a_, frame.bus_a, decision.scale);
  if (dual_bus_) {
    write_strip(strip_b_, frame.bus_b, decision.scale);
  }
}

const PowerDiagnostics &LedBus::power_diagnostics() const {
  return limiter_.diagnostics();
}

} // namespace led
