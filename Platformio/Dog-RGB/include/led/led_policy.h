#pragma once

#include <stdint.h>

#include "led/led_state.h"
#include "led/scene.h"

namespace led {

struct LedPolicyEffect {
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t speed;
  uint8_t intensity;
};

struct LedPolicyConfig {
  uint8_t brightness;
  uint16_t transition_ms;
  bool mirror_equal_effects;
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
  bool geofence_alert;
  bool wifi_off;
  bool home_set;
  bool geofence_distance_valid;
  bool homogeneous_ready;
  float speed_kph;
  uint8_t geofence_range;
  const SceneV1 *scene;
  bool scene_manual;
  uint32_t scene_activation_revision;
};

class LedPolicyEngine {
 public:
  LedState evaluate(const LedPolicyInput &input) const;
};

uint8_t policy_speed_range(const LedPolicyConfig &config, float kph);
Rgb policy_range_base_color(uint8_t range);

} // namespace led
