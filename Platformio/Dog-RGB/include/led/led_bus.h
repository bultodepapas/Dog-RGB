#pragma once

#include <Adafruit_NeoPixel.h>
#include <stdint.h>

#include "led/led_frame.h"
#include "led/power_limiter.h"

namespace led {

// Hardware boundary for both physical SK6812 buses. Renderers hand it one
// logical frame; this class applies the global electrical limit, performs the
// RGBW extraction and writes the strips.
class LedBus {
 public:
  LedBus(uint16_t pixel_count, int16_t pin_a, int16_t pin_b, bool dual_bus);

  void begin(uint8_t brightness);
  void set_brightness(uint8_t brightness);
  void configure_power(const PowerLimitConfig &config);
  void show(const LedFrame &frame);

  const PowerDiagnostics &power_diagnostics() const;

 private:
  void write_strip(Adafruit_NeoPixel &strip, const Rgb *pixels,
                   uint8_t scale);

  uint16_t pixel_count_;
  bool dual_bus_;
  uint8_t brightness_;
  Adafruit_NeoPixel strip_a_;
  Adafruit_NeoPixel strip_b_;
  PowerLimiter limiter_;
};

} // namespace led
