#include "led/led_policy.h"

#include "led/effect_registry.h"

namespace led {
namespace {

static uint8_t default_palette(uint8_t effect_id) {
  const EffectDescriptor *descriptor = effect_descriptor(effect_id);
  return descriptor == nullptr ? PALETTE_NONE : descriptor->default_palette_id;
}

static void select_effects(LedState &state, uint8_t effect_a,
                           uint8_t effect_b, bool mirror_equal_effects) {
  state.effect_a = effect_a;
  state.effect_b = effect_b;
  state.palette_a = default_palette(effect_a);
  state.palette_b = default_palette(effect_b);
  state.mirror = mirror_equal_effects && effect_a == effect_b;
}

static LedState base_state(LedMode mode, const LedPolicyConfig *config) {
  LedState state{};
  state.mode = mode;
  if (config != nullptr) {
    state.brightness = config->brightness;
    state.transition_ms = config->transition_ms;
    state.mirror = config->mirror_equal_effects;
  }
  return state;
}

static bool apply_scene(LedState &state, const SceneV1 *scene,
                        bool manual, uint32_t activation_revision) {
  if (scene == nullptr || !scene_validate(*scene)) return false;
  state.intent = manual ? LedIntent::SceneManual : LedIntent::Show;
  state.priority = state.critical_alert ? 90 : 30;
  state.mirror = scene->mirror;
  state.effect_a = scene->effect_a;
  state.effect_b = scene->effect_b;
  state.palette_a = scene->palette_a;
  state.palette_b = scene->palette_b;
  state.speed = scene->speed;
  state.intensity = scene->intensity;
  state.body_level = scene->body_level;
  state.transition_ms = scene->transition_ms;
  state.scene_id = scene->scene_id;
  state.scene_activation_revision = activation_revision;
  state.base = scene->base;
  state.accent = scene->accent;
  return true;
}

} // namespace

LedState LedPolicyEngine::evaluate(const LedPolicyInput &input) const {
  LedState state = base_state(input.mode, input.config);
  if (input.welcome_active) {
    state.intent = LedIntent::Welcome;
    state.priority = 100;
    state.status_enabled = false;
    state.homogeneous = true;
    state.mirror = true;
    return state;
  }
  if (input.critical_error || input.geofence_alert) {
    state.priority = 90;
    state.critical_alert = true;
    state.alert = input.critical_error ? LedAlert::System
                                       : LedAlert::Geofence;
  }
  if (input.day_mode_active) {
    state.intent = LedIntent::DayStatus;
    state.priority = state.critical_alert ? 90 : 80;
    state.body_enabled = false;
    return state;
  }

  state.homogeneous = input.wifi_off && input.homogeneous_ready;
  if (apply_scene(state, input.scene, input.scene_manual,
                  input.scene_activation_revision)) {
    return state;
  }

  if (input.config == nullptr) {
    return state;
  }
  const LedPolicyConfig &config = *input.config;

  if (input.mode == LedMode::Simple) {
    state.intent = LedIntent::Simple;
    state.priority = state.critical_alert ? 90 : 30;
    state.homogeneous = true;
    select_effects(state, config.simple.effect_a, config.simple.effect_b,
                   config.mirror_equal_effects);
    state.speed = config.simple.speed;
    state.intensity = config.simple.intensity;
    state.base = config.simple_base;
    state.accent = config.simple_base;
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
      select_effects(state, 2, 2, config.mirror_equal_effects);
      state.speed = 60;
      state.intensity = 120;
      state.base = {60, 45, 0};
      state.accent = state.base;
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
  state.priority = state.critical_alert ? 90 : 30;
  state.range = range;
  select_effects(state, config.range_effects[index].effect_a,
                 config.range_effects[index].effect_b,
                 config.mirror_equal_effects);
  state.speed = config.range_effects[index].speed;
  state.intensity = config.range_effects[index].intensity;
  state.base = policy_range_base_color(range);
  state.accent = state.base;
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
