#pragma once

#include <stdint.h>

#include "config.h"
#include "led/led_color.h"

namespace led {

// Logical frame rendered by the effects engine. The transport layer owns all
// hardware details and converts this RGB representation to the physical RGBW
// order only when the frame is sent.
struct LedFrame {
  Rgb bus_a[LED_STRIP_COUNT];
  Rgb bus_b[LED_STRIP_COUNT];
};

Rgbw rgb_to_rgbw(const Rgb &color);
Rgbw scale_rgbw(const Rgbw &color, uint8_t scale);

} // namespace led
