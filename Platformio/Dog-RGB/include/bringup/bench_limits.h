#pragma once

#include <stdint.h>
#include "led/power_limiter.h"

// Transport-only overrides for the I2 diagnostic. They never change NVS or
// RuntimeConfig, and cover every caller, including portal apply callbacks.
namespace bringup::bench_limits {
constexpr uint8_t kMaxBrightness = 16;
constexpr uint8_t brightness(uint8_t requested) {
  return requested < kMaxBrightness ? requested : kMaxBrightness;
}

inline led::PowerLimitConfig power(const led::PowerLimitConfig &requested) {
  led::PowerLimitConfig effective = requested;
  effective.enabled = true;
  if (effective.budget_ma > LED_POWER_BUDGET_MA_DEFAULT) {
    effective.budget_ma = LED_POWER_BUDGET_MA_DEFAULT;
  }
  return effective;
}
} // namespace bringup::bench_limits
