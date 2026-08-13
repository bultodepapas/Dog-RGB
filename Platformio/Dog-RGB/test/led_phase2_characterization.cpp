#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "led/effect_registry.h"
#include "led/led_policy.h"
#include "led/led_state.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

void hash_byte(uint64_t &hash, uint8_t value) {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

uint64_t effect_digest(uint8_t id) {
  constexpr size_t PIXEL_COUNT = 32;
  constexpr uint16_t START = 3;
  constexpr uint16_t COUNT = 24;
  const uint32_t times[] = {0, 137, 733, 1589, 3203};

  std::array<led::Rgb, PIXEL_COUNT> pixels{};
  std::array<uint8_t, PIXEL_COUNT> heat{};
  for (size_t i = 0; i < PIXEL_COUNT; ++i) {
    pixels[i] = led::Rgb{static_cast<uint8_t>(3U + i * 5U),
                         static_cast<uint8_t>(7U + i * 3U),
                         static_cast<uint8_t>(11U + i * 7U)};
    heat[i] = static_cast<uint8_t>(3U + i * 9U);
  }
  const auto initial_pixels = pixels;
  led::EffectRuntime runtime{17, 5};
  uint32_t random_state = UINT32_C(0x12345678) ^
                          (static_cast<uint32_t>(id) * UINT32_C(0x9E3779B9));
  uint64_t digest = UINT64_C(14695981039346656037);

  for (uint32_t now_ms : times) {
    led::EffectRenderContext context{pixels.data(), heat.data(), START, COUNT,
                                     {91, 53, 17}, 137, 173, now_ms,
                                     &random_state, &runtime};
    led::render_effect(id, context);

    for (uint16_t i = 0; i < START; ++i) {
      expect(std::memcmp(&pixels[i], &initial_pixels[i], sizeof(led::Rgb)) == 0,
             "effect wrote before its segment");
    }
    for (uint16_t i = START + COUNT; i < PIXEL_COUNT; ++i) {
      expect(std::memcmp(&pixels[i], &initial_pixels[i], sizeof(led::Rgb)) == 0,
             "effect wrote after its segment");
    }

    for (const led::Rgb &pixel : pixels) {
      hash_byte(digest, pixel.r);
      hash_byte(digest, pixel.g);
      hash_byte(digest, pixel.b);
    }
    for (uint8_t value : heat) hash_byte(digest, value);
    hash_byte(digest, runtime.hue);
    hash_byte(digest, static_cast<uint8_t>(runtime.pos & 0xFFU));
    hash_byte(digest, static_cast<uint8_t>((runtime.pos >> 8U) & 0xFFU));
    for (uint8_t shift = 0; shift < 32; shift += 8) {
      hash_byte(digest, static_cast<uint8_t>((random_state >> shift) & 0xFFU));
    }
  }
  return digest;
}

void test_registry(bool print_goldens) {
  static const char *const KEYS[led::EFFECT_REGISTRY_COUNT] = {
      "solid", "pulse", "breath", "chase", "comet", "sinelon",
      "confetti", "juggle", "bpm", "rainbow", "fire", "gradient_wave"};
  static const char *const LABELS[led::EFFECT_REGISTRY_COUNT] = {
      "SOLID", "PULSE", "BREATH", "CHASE", "COMET", "SINELON",
      "CONFETTI", "JUGGLE", "BPM", "RAINBOW", "FIRE", "GRADIENT_WAVE"};
  static const uint8_t CONTROLS[led::EFFECT_REGISTRY_COUNT] = {
      led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_INTENSITY |
          led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_INTENSITY |
          led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_INTENSITY |
          led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_INTENSITY | led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_INTENSITY |
          led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED | led::EFFECT_CONTROL_COLOR,
      led::EFFECT_CONTROL_SPEED,
      led::EFFECT_CONTROL_INTENSITY,
      led::EFFECT_CONTROL_SPEED,
  };
  static const led::EffectDefaults DEFAULTS[led::EFFECT_REGISTRY_COUNT] = {
      {80, 140},  {80, 140},  {60, 90},   {120, 140},
      {120, 140}, {110, 150}, {100, 150}, {150, 180},
      {100, 150}, {120, 180}, {155, 200}, {120, 180},
  };
  static const led::EffectSafetyClass SAFETY[led::EFFECT_REGISTRY_COUNT] = {
      led::EffectSafetyClass::Calm,    led::EffectSafetyClass::Active,
      led::EffectSafetyClass::Calm,    led::EffectSafetyClass::Active,
      led::EffectSafetyClass::Active,  led::EffectSafetyClass::Active,
      led::EffectSafetyClass::Active,  led::EffectSafetyClass::Advanced,
      led::EffectSafetyClass::Active,  led::EffectSafetyClass::Active,
      led::EffectSafetyClass::Advanced, led::EffectSafetyClass::Active,
  };
  // Golden FNV-1a digests for five frames at fixed times, seed, segment,
  // initial pixels and heat. These are the visual behavior contract for the
  // twelve persisted IDs; intentional renderer changes must update them.
  static const uint64_t GOLDENS[led::EFFECT_REGISTRY_COUNT] = {
      UINT64_C(0x991b298856e4bc79), UINT64_C(0x30bcb6d0e258100e),
      UINT64_C(0x9d258dec9339ff58), UINT64_C(0x70f5550d987f00b4),
      UINT64_C(0xb32edae9c189b5f5), UINT64_C(0x36653e8e1ce5fc81),
      UINT64_C(0x341b431c941206f6), UINT64_C(0xd63bc95c7688dca9),
      UINT64_C(0xfd3327f33ed762e6), UINT64_C(0x7b738d4f0a769a5b),
      UINT64_C(0xf02214b60db1e9bc), UINT64_C(0x70d2087545139cd7),
  };

  expect(led::EFFECT_REGISTRY_VERSION == 1, "unexpected registry version");
  expect(led::effect_descriptor_count() == led::EFFECT_REGISTRY_COUNT,
         "registry count differs from its public contract");
  expect(led::effect_descriptor(led::EFFECT_REGISTRY_COUNT) == nullptr,
         "out-of-range effect id must be rejected");
  expect(!led::effect_id_valid(-1), "negative effect id must be rejected");
  expect(!led::effect_id_valid(led::EFFECT_REGISTRY_COUNT),
         "first unregistered effect id must be rejected");
  expect(!led::effect_id_valid(255), "uint8 maximum must be rejected");
  expect(!led::effect_id_valid(256),
         "effect id must not wrap through uint8 conversion");
  expect(led::effect_descriptor_by_key(nullptr) == nullptr,
         "null effect key must be rejected");
  expect(led::effect_descriptor_by_key("not-an-effect") == nullptr,
         "unknown effect key must be rejected");

  for (uint8_t id = 0; id < led::EFFECT_REGISTRY_COUNT; ++id) {
    const led::EffectDescriptor *descriptor = led::effect_descriptor(id);
    expect(descriptor != nullptr, "registered id is not addressable");
    if (descriptor == nullptr) continue;
    expect(descriptor->id == id, "effect id changed position");
    expect(std::strcmp(descriptor->key, KEYS[id]) == 0,
           "stable effect key changed");
    expect(std::strcmp(descriptor->label, LABELS[id]) == 0,
           "effect presentation label changed");
    expect(descriptor->controls == CONTROLS[id],
           "effect controls are not the characterized contract");
    expect(descriptor->defaults.speed == DEFAULTS[id].speed &&
               descriptor->defaults.intensity == DEFAULTS[id].intensity,
           "effect defaults changed");
    expect(descriptor->safety == SAFETY[id],
           "effect safety class changed");
    const led::EffectColorMode expected_color =
        id >= 9 ? led::EffectColorMode::Generated
                : led::EffectColorMode::Base;
    expect(descriptor->color_mode == expected_color,
           "effect color mode changed");
    expect(led::effect_descriptor_by_key(KEYS[id]) == descriptor,
           "key lookup is not canonical");
    expect(&led::effect_descriptor_at(id) == descriptor,
           "indexed lookup is not canonical");
    if ((descriptor->controls & led::EFFECT_CONTROL_SPEED) != 0) {
      expect(descriptor->defaults.speed >= descriptor->useful.speed_min &&
                 descriptor->defaults.speed <= descriptor->useful.speed_max,
             "default speed lies outside useful range");
    }
    if ((descriptor->controls & led::EFFECT_CONTROL_INTENSITY) != 0) {
      expect(descriptor->defaults.intensity >= descriptor->useful.intensity_min &&
                 descriptor->defaults.intensity <= descriptor->useful.intensity_max,
             "default intensity lies outside useful range");
    }
    if (descriptor->color_mode == led::EffectColorMode::Generated) {
      expect((descriptor->controls & led::EFFECT_CONTROL_COLOR) == 0,
             "generated-color effect must not advertise a color control");
    }
    const led::EffectPaletteMode expected_palette =
        id == 10 ? led::EffectPaletteMode::Internal
                 : led::EffectPaletteMode::None;
    expect(descriptor->palette_mode == expected_palette,
           "effect palette usage metadata changed");
    expect(descriptor->palette_mode != led::EffectPaletteMode::Selectable,
           "phase-2 effects unexpectedly claim selectable palette support");

    const uint64_t actual = effect_digest(id);
    if (print_goldens) {
      std::printf("UINT64_C(0x%016" PRIx64 "),\n", actual);
    } else {
      expect(actual == GOLDENS[id], "effect output digest changed");
    }
  }
}

led::LedPolicyConfig policy_config() {
  led::LedPolicyConfig config{};
  config.brightness = 123;
  for (uint8_t i = 0; i < 9; ++i) {
    config.speed_ranges_kph[i] = static_cast<float>(i + 1U);
  }
  for (uint8_t i = 0; i < 10; ++i) {
    config.range_effects[i] = led::LedPolicyEffect{
        static_cast<uint8_t>(i), static_cast<uint8_t>(i + 1U),
        static_cast<uint8_t>(20U + i), static_cast<uint8_t>(100U + i)};
  }
  config.simple = led::LedPolicyEffect{9, 9, 66, 77};
  config.simple_base = led::Rgb{12, 34, 56};
  return config;
}

led::LedPolicyInput policy_input(const led::LedPolicyConfig *config) {
  led::LedPolicyInput input{};
  input.mode = led::LedMode::Speed;
  input.config = config;
  input.gps_ok = true;
  input.home_set = true;
  input.geofence_distance_valid = true;
  input.homogeneous_ready = true;
  input.speed_kph = 0.5F;
  input.geofence_range = 1;
  input.show_effect = 7;
  input.show_speed = 88;
  input.show_intensity = 99;
  input.show_base = led::Rgb{4, 5, 6};
  return input;
}

void test_policy() {
  const led::LedPolicyConfig config = policy_config();
  led::LedPolicyInput input = policy_input(&config);
  const led::LedPolicyEngine engine;

  led::LedState state = engine.evaluate(input);
  expect(state.brightness == 123,
         "policy state must retain the configured brightness");
  expect(state.intent == led::LedIntent::Range && state.range == 1,
         "speed policy did not select range 1");
  expect(state.effect_a == 0 && state.effect_b == 1 && state.priority == 30,
         "range policy did not copy its configured effect");

  input.speed_kph = 9.5F;
  state = engine.evaluate(input);
  expect(state.range == 10 && state.effect_a == 9 && state.effect_b == 10,
         "speed policy did not select range 10");

  input.welcome_active = true;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Welcome && state.priority == 100 &&
             state.homogeneous && !state.status_enabled,
         "welcome priority contract changed");
  input.welcome_active = false;

  input.day_mode_active = true;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::DayStatus && state.priority == 80 &&
             !state.body_enabled,
         "day-mode priority contract changed");
  input.critical_error = true;
  state = engine.evaluate(input);
  expect(state.priority == 90 && state.critical_alert,
         "critical alert did not override day-mode priority");
  input.day_mode_active = false;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Range && state.priority == 90 &&
             state.critical_alert,
         "critical overlay removed the decorative body intent");
  input.critical_error = false;

  input.gps_ok = false;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Idle && state.priority == 20 &&
             state.range == -1,
         "missing GPS did not select idle");
  input.gps_ok = true;

  input.mode = led::LedMode::Geofence;
  input.home_set = false;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::HomeMissing && state.effect_a == 2 &&
             state.speed == 60 && state.range == -1,
         "missing home did not select its guidance effect");
  input.home_set = true;
  input.geofence_distance_valid = false;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Idle && state.range == -1,
         "invalid geofence distance did not select idle");
  input.geofence_distance_valid = true;
  input.geofence_range = 4;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Range && state.range == 4 &&
             state.effect_a == 3,
         "geofence range did not use the shared effect policy");

  input.mode = led::LedMode::Show;
  input.config = nullptr;
  input.wifi_off = true;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Show && state.effect_a == 7 &&
             state.effect_b == 7 && state.homogeneous,
         "show policy depends on persisted range configuration");

  input.mode = led::LedMode::Simple;
  input.config = &config;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Simple && state.effect_a == 9 &&
             state.speed == 66 && state.intensity == 77 && state.homogeneous &&
             !state.status_enabled && state.base.r == 12,
         "simple policy contract changed");

  input.mode = led::LedMode::Speed;
  input.config = nullptr;
  state = engine.evaluate(input);
  expect(state.intent == led::LedIntent::Idle && state.priority == 20,
         "null configuration is not handled safely");

  expect(led::policy_speed_range(config, 1.0F) == 1 &&
             led::policy_speed_range(config, 1.01F) == 2 &&
             led::policy_speed_range(config, 100.0F) == 10,
         "speed threshold boundary contract changed");
  expect(std::strcmp(led::led_intent_name(led::LedIntent::HomeMissing),
                     "home_missing") == 0,
         "intent serialization contract changed");
  expect(std::strcmp(led::led_mode_name(led::LedMode::Geofence),
                     "geofence") == 0,
         "mode serialization contract changed");
}

}  // namespace

int main(int argc, char **argv) {
  const bool print_goldens = argc == 2 &&
                             std::strcmp(argv[1], "--print-goldens") == 0;
  test_registry(print_goldens);
  test_policy();
  if (failures != 0) {
    std::fprintf(stderr, "led_phase2_characterization: %d failure(s)\n", failures);
    return 1;
  }
  if (!print_goldens) std::puts("led_phase2_characterization: ok");
  return 0;
}
