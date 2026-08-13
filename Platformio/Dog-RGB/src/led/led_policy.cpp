#include "led/led_policy.h"

namespace led {
namespace {

static LedState base_state(LedMode mode, uint8_t brightness) {
  return LedState{mode, LedIntent::Idle, 20, brightness, true, true, false,
                  false, -1, 9, 9, 80, 140, {0, 60, 60}};
}

} // namespace

LedState LedPolicyEngine::evaluate(const LedPolicyInput &input) const {
  const uint8_t brightness =
      input.config == nullptr ? 1 : input.config->brightness;
  LedState state = base_state(input.mode, brightness);
  if (input.welcome_active) {
    state.intent = LedIntent::Welcome;
    state.priority = 100;
    state.status_enabled = false;
    state.homogeneous = true;
    return state;
  }
  if (input.critical_error) {
    state.priority = 90;
    state.critical_alert = true;
  }
  if (input.day_mode_active) {
    state.intent = LedIntent::DayStatus;
    state.priority = input.critical_error ? 90 : 80;
    state.body_enabled = false;
    state.critical_alert = input.critical_error;
    return state;
  }

  state.homogeneous = input.wifi_off && input.homogeneous_ready;
  state.critical_alert = input.critical_error;
  if (input.mode == LedMode::Show) {
    state.intent = LedIntent::Show;
    state.priority = input.critical_error ? 90 : 30;
    state.effect_a = input.show_effect;
    state.effect_b = input.show_effect;
    state.speed = input.show_speed;
    state.intensity = input.show_intensity;
    state.base = input.show_base;
    return state;
  }

  if (input.config == nullptr) {
    return state;
  }
  const LedPolicyConfig &config = *input.config;

  if (input.mode == LedMode::Simple) {
    state.intent = LedIntent::Simple;
    state.priority = input.critical_error ? 90 : 30;
    state.homogeneous = true;
    state.status_enabled = false;
    state.effect_a = config.simple.effect_a;
    state.effect_b = config.simple.effect_b;
    state.speed = config.simple.speed;
    state.intensity = config.simple.intensity;
    state.base = config.simple_base;
    return state;
  }

  uint8_t range = 1;
  if (input.mode == LedMode::Geofence) {
    if (!input.gps_ok) {
      state.intent = LedIntent::Idle;
      state.range = -1;
      return state;
    }
    if (!input.home_set) {
      state.intent = LedIntent::HomeMissing;
      state.effect_a = 2;
      state.effect_b = 2;
      state.speed = 60;
      state.intensity = 120;
      state.base = {60, 45, 0};
      state.range = -1;
      return state;
    }
    if (!input.geofence_distance_valid) {
      state.intent = LedIntent::Idle;
      state.range = -1;
      return state;
    }
    range = input.geofence_range;
  } else {
    if (!input.gps_ok) {
      state.intent = LedIntent::Idle;
      state.range = -1;
      return state;
    }
    range = policy_speed_range(config, input.speed_kph);
  }

  const uint8_t index = (range >= 1 && range <= 10) ? range - 1U : 0U;
  state.intent = LedIntent::Range;
  state.priority = input.critical_error ? 90 : 30;
  state.range = range;
  state.effect_a = config.range_effects[index].effect_a;
  state.effect_b = config.range_effects[index].effect_b;
  state.speed = config.range_effects[index].speed;
  state.intensity = config.range_effects[index].intensity;
  state.base = policy_range_base_color(range);
  return state;
}

uint8_t policy_speed_range(const LedPolicyConfig &config, float kph) {
  for (uint8_t i = 0; i < 9; ++i) {
    if (kph <= config.speed_ranges_kph[i]) {
      return static_cast<uint8_t>(i + 1U);
    }
  }
  return 10;
}

Rgb policy_range_base_color(uint8_t range) {
  static const Rgb COLORS[10] = {
      {0, 60, 60}, {0, 60, 35}, {0, 60, 0}, {25, 60, 0}, {60, 60, 0},
      {60, 45, 0}, {60, 30, 0}, {60, 20, 0}, {60, 10, 0}, {60, 0, 0}};
  return COLORS[(range >= 1 && range <= 10) ? range - 1U : 0U];
}

} // namespace led
