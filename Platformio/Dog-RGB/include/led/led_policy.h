#pragma once

#include <stdint.h>

#include "led/led_state.h"

namespace led {

struct LedPolicyEffect {
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t speed;
  uint8_t intensity;
};

struct LedPolicyConfig {
  uint8_t brightness;
  float speed_ranges_kph[9];
  LedPolicyEffect range_effects[10];
  LedPolicyEffect simple;
  Rgb simple_base;
};

struct LedPolicyInput {
  LedMode mode;
  const LedPolicyConfig *config;
  bool welcome_active;
  bool day_mode_active;
  bool gps_ok;
  bool critical_error;
  bool wifi_off;
  bool home_set;
  bool geofence_distance_valid;
  bool homogeneous_ready;
  float speed_kph;
  uint8_t geofence_range;
  uint8_t show_effect;
  uint8_t show_speed;
  uint8_t show_intensity;
  Rgb show_base;
};

class LedPolicyEngine {
 public:
  LedState evaluate(const LedPolicyInput &input) const;
};

uint8_t policy_speed_range(const LedPolicyConfig &config, float kph);
Rgb policy_range_base_color(uint8_t range);

} // namespace led
