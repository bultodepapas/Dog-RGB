#pragma once

#include <stdint.h>

#include "led/led_frame.h"

namespace led {

struct PowerLimitConfig {
  bool enabled;
  uint16_t budget_ma;
  uint16_t base_current_ma;
  uint8_t rgb_channel_ma;
  uint8_t white_channel_ma;
};

struct PowerDiagnostics {
  uint16_t requested_ma;
  uint16_t estimated_ma;
  uint16_t peak_requested_ma;
  uint8_t scale;
  uint32_t frames_limited;
};

struct PowerLimitDecision {
  uint8_t scale;
  uint16_t requested_ma;
  uint16_t estimated_ma;
};

class PowerLimiter {
 public:
  PowerLimiter();

  void configure(const PowerLimitConfig &config);
  PowerLimitDecision evaluate(const LedFrame &frame, uint8_t brightness,
                              uint8_t active_buses);
  const PowerDiagnostics &diagnostics() const;

 private:
  uint16_t estimate_ma(const LedFrame &frame, uint8_t brightness,
                       uint8_t active_buses, uint8_t scale) const;

  PowerLimitConfig config_;
  PowerDiagnostics diagnostics_;
  uint8_t applied_scale_;
};

} // namespace led
