#include "led/power_limiter.h"

#include <limits.h>

namespace led {
namespace {
static const uint8_t LIMIT_RELEASE_STEP = 8;

uint8_t apply_scale(uint8_t value, uint8_t scale) {
  if (scale == 255) {
    return value;
  }
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale) / 255U);
}

uint8_t apply_brightness(uint8_t value, uint8_t brightness) {
  // Match Adafruit_NeoPixel exactly: brightness 255 is stored internally as
  // zero and means no scaling; every other value uses (brightness + 1) / 256.
  if (brightness == 255) {
    return value;
  }
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(value) * (static_cast<uint16_t>(brightness) + 1U)) >> 8);
}
} // namespace

PowerLimiter::PowerLimiter()
    : config_{LED_POWER_LIMIT_ENABLED_DEFAULT,
              LED_POWER_BUDGET_MA_DEFAULT,
              LED_BASE_CURRENT_MA_DEFAULT,
              LED_RGB_CHANNEL_MA_DEFAULT,
              LED_WHITE_CHANNEL_MA_DEFAULT},
      diagnostics_{0, 0, 0, 255, 0},
      applied_scale_(255) {}

void PowerLimiter::configure(const PowerLimitConfig &config) {
  config_ = config;
  if (!config_.enabled) {
    applied_scale_ = 255;
  }
}

uint16_t PowerLimiter::estimate_ma(const LedFrame &frame, uint8_t brightness,
                                   uint8_t active_buses, uint8_t scale) const {
  const uint8_t bus_count = active_buses > 1 ? 2 : 1;
  uint32_t weighted_current = 0;

  for (uint8_t bus = 0; bus < bus_count; ++bus) {
    const Rgb *pixels = bus == 0 ? frame.bus_a : frame.bus_b;
    for (int i = 0; i < LED_STRIP_COUNT; ++i) {
      const Rgbw converted = rgb_to_rgbw(pixels[i]);
      const uint8_t r = apply_brightness(apply_scale(converted.r, scale), brightness);
      const uint8_t g = apply_brightness(apply_scale(converted.g, scale), brightness);
      const uint8_t b = apply_brightness(apply_scale(converted.b, scale), brightness);
      const uint8_t w = apply_brightness(apply_scale(converted.w, scale), brightness);
      weighted_current +=
          static_cast<uint32_t>(r + g + b) * config_.rgb_channel_ma +
          static_cast<uint32_t>(w) * config_.white_channel_ma;
    }
  }

  // Channel current is specified at value 255. Round upward so the model is
  // conservative rather than silently accepting a fractional over-budget mA.
  const uint32_t led_current_ma = (weighted_current + 254U) / 255U;
  const uint32_t total_ma = static_cast<uint32_t>(config_.base_current_ma) +
                            led_current_ma;
  return total_ma > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(total_ma);
}

PowerLimitDecision PowerLimiter::evaluate(const LedFrame &frame,
                                          uint8_t brightness,
                                          uint8_t active_buses) {
  const uint16_t requested_ma = estimate_ma(frame, brightness, active_buses, 255);
  uint8_t safe_scale = 255;

  if (config_.enabled && requested_ma > config_.budget_ma) {
    // Find the brightest integer scale whose conservative estimate stays at
    // or below the global budget. Binary search keeps the hot path bounded.
    uint16_t low = 0;
    uint16_t high = 255;
    while (low < high) {
      const uint16_t mid = static_cast<uint16_t>((low + high + 1U) / 2U);
      if (estimate_ma(frame, brightness, active_buses,
                      static_cast<uint8_t>(mid)) <= config_.budget_ma) {
        low = mid;
      } else {
        high = static_cast<uint16_t>(mid - 1U);
      }
    }
    safe_scale = static_cast<uint8_t>(low);
  }

  if (!config_.enabled) {
    applied_scale_ = 255;
  } else if (safe_scale < applied_scale_) {
    // Reduce immediately for safety.
    applied_scale_ = safe_scale;
  } else if (safe_scale > applied_scale_) {
    // Recover brightness gradually to avoid visible pumping as an animation
    // crosses the budget boundary. This can only stay below the safe scale.
    const uint16_t released = static_cast<uint16_t>(applied_scale_) +
                              LIMIT_RELEASE_STEP;
    applied_scale_ = static_cast<uint8_t>(released < safe_scale
                                              ? released
                                              : safe_scale);
  }

  const uint16_t estimated_ma =
      estimate_ma(frame, brightness, active_buses, applied_scale_);
  diagnostics_.requested_ma = requested_ma;
  diagnostics_.estimated_ma = estimated_ma;
  if (requested_ma > diagnostics_.peak_requested_ma) {
    diagnostics_.peak_requested_ma = requested_ma;
  }
  diagnostics_.scale = applied_scale_;
  if (config_.enabled && applied_scale_ < 255 &&
      diagnostics_.frames_limited < UINT32_MAX) {
    diagnostics_.frames_limited++;
  }

  return PowerLimitDecision{applied_scale_, requested_ma, estimated_ma};
}

const PowerDiagnostics &PowerLimiter::diagnostics() const {
  return diagnostics_;
}

} // namespace led
