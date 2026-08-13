#pragma once

#include <stdint.h>

#include "config.h"
#include "led/led_color.h"

namespace led {

// Fixed RGB frame used at both logical-render and composed-physical boundaries.
// The transport layer converts the final composed instance to RGBW only when
// it is sent; no renderer owns the NeoPixel representation.
struct LedFrame {
  Rgb bus_a[LED_STRIP_COUNT];
  Rgb bus_b[LED_STRIP_COUNT];
};

} // namespace led
