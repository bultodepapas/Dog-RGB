#include "led/effect_registry.h"

#include <math.h>
#include <string.h>

namespace led {
namespace {

static const EffectDescriptor EFFECTS[EFFECT_REGISTRY_COUNT] = {
    {0, "solid", "SOLID", EFFECT_CONTROL_COLOR,
     {80, 140}, {0, 255, 0, 255}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Calm},
    {1, "pulse", "PULSE", EFFECT_CONTROL_SPEED | EFFECT_CONTROL_COLOR,
     {80, 140}, {10, 180, 0, 255}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {2, "breath", "BREATH", EFFECT_CONTROL_SPEED | EFFECT_CONTROL_COLOR,
     {60, 90}, {10, 140, 0, 255}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Calm},
    {3, "chase", "CHASE",
     EFFECT_CONTROL_SPEED | EFFECT_CONTROL_INTENSITY | EFFECT_CONTROL_COLOR,
     {120, 140}, {32, 224, 40, 240}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {4, "comet", "COMET",
     EFFECT_CONTROL_SPEED | EFFECT_CONTROL_INTENSITY | EFFECT_CONTROL_COLOR,
     {120, 140}, {24, 216, 40, 240}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {5, "sinelon", "SINELON",
     EFFECT_CONTROL_SPEED | EFFECT_CONTROL_INTENSITY | EFFECT_CONTROL_COLOR,
     {110, 150}, {20, 180, 40, 240}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {6, "confetti", "CONFETTI",
     EFFECT_CONTROL_INTENSITY | EFFECT_CONTROL_COLOR,
     {100, 150}, {0, 255, 40, 240}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {7, "juggle", "JUGGLE",
     EFFECT_CONTROL_SPEED | EFFECT_CONTROL_INTENSITY | EFFECT_CONTROL_COLOR,
     {150, 180}, {20, 200, 40, 240}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Advanced},
    {8, "bpm", "BPM", EFFECT_CONTROL_SPEED | EFFECT_CONTROL_COLOR,
     {100, 150}, {10, 180, 0, 255}, EffectColorMode::Base,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {9, "rainbow", "RAINBOW", EFFECT_CONTROL_SPEED,
     {120, 180}, {16, 224, 0, 255}, EffectColorMode::Generated,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
    {10, "fire", "FIRE", EFFECT_CONTROL_INTENSITY,
     {155, 200}, {80, 220, 80, 240}, EffectColorMode::Generated,
     EffectPaletteMode::Internal,
     EffectSafetyClass::Advanced},
    {11, "gradient_wave", "GRADIENT_WAVE", EFFECT_CONTROL_SPEED,
     {120, 180}, {24, 224, 0, 255}, EffectColorMode::Generated,
     EffectPaletteMode::None,
     EffectSafetyClass::Active},
};

static uint8_t clamp_u8(uint16_t value) {
  return value > 255U ? 255U : static_cast<uint8_t>(value);
}

static uint8_t scale8(uint8_t value, uint8_t scale) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale) / 255U);
}

static void fade(Rgb &color, uint8_t amount) {
  const uint8_t scale = static_cast<uint8_t>(255U - amount);
  color.r = scale8(color.r, scale);
  color.g = scale8(color.g, scale);
  color.b = scale8(color.b, scale);
}

static void add(Rgb &target, const Rgb &source) {
  target.r = clamp_u8(static_cast<uint16_t>(target.r) + source.r);
  target.g = clamp_u8(static_cast<uint16_t>(target.g) + source.g);
  target.b = clamp_u8(static_cast<uint16_t>(target.b) + source.b);
}

static void fill(EffectRenderContext &ctx, const Rgb &color) {
  for (uint16_t i = ctx.start; i < ctx.start + ctx.count; ++i) {
    ctx.pixels[i] = color;
  }
}

static void fade_range(EffectRenderContext &ctx, uint8_t amount) {
  for (uint16_t i = ctx.start; i < ctx.start + ctx.count; ++i) {
    fade(ctx.pixels[i], amount);
  }
}

static uint8_t step_from_speed(uint8_t speed, uint8_t divisor) {
  const uint8_t step = static_cast<uint8_t>(speed / divisor);
  return step < 1 ? 1 : step;
}

static uint32_t next_random(uint32_t &state) {
  // Fixed xorshift32: small, deterministic and independent from Arduino's
  // process-global PRNG. Avoid zero becoming a locked state.
  if (state == 0) {
    state = 0x6D2B79F5UL;
  }
  uint32_t value = state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  state = value;
  return value;
}

static uint8_t random8(uint32_t &state, uint8_t max_value) {
  return static_cast<uint8_t>(next_random(state) %
                              (static_cast<uint16_t>(max_value) + 1U));
}

static uint8_t random8(uint32_t &state, uint8_t min_value,
                       uint8_t max_value) {
  if (max_value <= min_value) {
    return min_value;
  }
  const uint16_t span = static_cast<uint16_t>(max_value - min_value) + 1U;
  return static_cast<uint8_t>(min_value + (next_random(state) % span));
}

static uint8_t hsv_to_rgb_component(uint8_t hue, uint8_t sat, uint8_t val,
                                    uint8_t component) {
  const uint8_t region = hue / 43U;
  const uint8_t remainder = static_cast<uint8_t>((hue - (region * 43U)) * 6U);
  const uint8_t p = scale8(val, static_cast<uint8_t>(255U - sat));
  const uint8_t q = scale8(
      val, static_cast<uint8_t>(255U - scale8(sat, remainder)));
  const uint8_t t = scale8(
      val, static_cast<uint8_t>(255U - scale8(sat,
                                               static_cast<uint8_t>(255U - remainder))));
  const Rgb colors[6] = {{val, t, p}, {q, val, p}, {p, val, t},
                         {p, q, val}, {t, p, val}, {val, p, q}};
  const Rgb color = colors[region < 6 ? region : 5];
  return component == 0 ? color.r : (component == 1 ? color.g : color.b);
}

static Rgb hsv_to_rgb(uint8_t hue, uint8_t sat, uint8_t val) {
  return Rgb{hsv_to_rgb_component(hue, sat, val, 0),
             hsv_to_rgb_component(hue, sat, val, 1),
             hsv_to_rgb_component(hue, sat, val, 2)};
}

static uint16_t beat(uint16_t bpm, uint16_t low, uint16_t high,
                     uint32_t now_ms) {
  const float phase = 6.28318530717958647692f *
                      (static_cast<float>(bpm) / 60.0f) *
                      (static_cast<float>(now_ms) / 1000.0f);
  const float norm = (sinf(phase) + 1.0f) * 0.5f;
  return static_cast<uint16_t>(low + (high - low) * norm);
}

static Rgb heat_color(uint8_t temperature) {
  const uint8_t t192 = static_cast<uint8_t>(
      (static_cast<uint16_t>(temperature) * 191U) / 255U);
  const uint8_t ramp = static_cast<uint8_t>((t192 & 0x3FU) << 2U);
  if (t192 > 0x80U) return Rgb{255, 255, ramp};
  if (t192 > 0x40U) return Rgb{255, ramp, 0};
  return Rgb{ramp, 0, 0};
}

static void render_fire(EffectRenderContext &ctx, uint32_t &random_state) {
  const uint8_t cooling = static_cast<uint8_t>(
      20U + (static_cast<uint16_t>(255U - ctx.intensity) * 60U) / 255U);
  const uint8_t sparking = static_cast<uint8_t>(
      20U + (static_cast<uint16_t>(ctx.intensity) * 100U) / 255U);
  for (uint16_t i = ctx.start; i < ctx.start + ctx.count; ++i) {
    const uint8_t maximum = static_cast<uint8_t>(
        ((static_cast<uint16_t>(cooling) * 10U) / ctx.count) + 2U);
    const uint8_t loss = random8(random_state, 0, maximum);
    ctx.heat[i] = ctx.heat[i] > loss
                      ? static_cast<uint8_t>(ctx.heat[i] - loss)
                      : 0;
  }
  for (int i = static_cast<int>(ctx.start + ctx.count) - 1;
       i >= static_cast<int>(ctx.start) + 2; --i) {
    ctx.heat[i] = static_cast<uint8_t>(
        (ctx.heat[i - 1] + ctx.heat[i - 2] + ctx.heat[i - 2]) / 3U);
  }
  if (random8(random_state, 255) < sparking) {
    const uint8_t spark_offset_max = ctx.count > 7
                                         ? 7
                                         : static_cast<uint8_t>(ctx.count - 1U);
    const uint16_t index = static_cast<uint16_t>(
        ctx.start + random8(random_state, spark_offset_max));
    const uint16_t heat = static_cast<uint16_t>(ctx.heat[index]) +
                          random8(random_state, 160, 255);
    ctx.heat[index] = clamp_u8(heat);
  }
  for (uint16_t i = ctx.start; i < ctx.start + ctx.count; ++i) {
    ctx.pixels[i] = heat_color(ctx.heat[i]);
  }
}

} // namespace

const EffectDescriptor *effect_descriptor(uint8_t id) {
  return id < EFFECT_REGISTRY_COUNT ? &EFFECTS[id] : nullptr;
}

const EffectDescriptor *effect_descriptor_by_key(const char *key) {
  if (key == nullptr) return nullptr;
  for (size_t i = 0; i < EFFECT_REGISTRY_COUNT; ++i) {
    if (strcmp(EFFECTS[i].key, key) == 0) return &EFFECTS[i];
  }
  return nullptr;
}

const EffectDescriptor &effect_descriptor_at(size_t index) {
  return EFFECTS[index < EFFECT_REGISTRY_COUNT ? index : 0];
}

size_t effect_descriptor_count() {
  return EFFECT_REGISTRY_COUNT;
}

bool effect_id_valid(int32_t id) {
  return id >= 0 && id <= UINT8_MAX &&
         effect_descriptor(static_cast<uint8_t>(id)) != nullptr;
}

const char *effect_safety_name(EffectSafetyClass safety) {
  switch (safety) {
    case EffectSafetyClass::Calm: return "calm";
    case EffectSafetyClass::Active: return "active";
    case EffectSafetyClass::Advanced: return "advanced";
  }
  return "unknown";
}

const char *effect_color_mode_name(EffectColorMode mode) {
  return mode == EffectColorMode::Generated ? "generated" : "base";
}

const char *effect_palette_mode_name(EffectPaletteMode mode) {
  switch (mode) {
    case EffectPaletteMode::Internal: return "internal";
    case EffectPaletteMode::Selectable: return "selectable";
    case EffectPaletteMode::None: return "none";
  }
  return "unknown";
}

void render_effect(uint8_t id, EffectRenderContext &ctx) {
  if (ctx.pixels == nullptr || ctx.runtime == nullptr || ctx.count == 0) {
    return;
  }
  uint32_t fallback_random = 0xA341316CUL;
  uint32_t &random_state = ctx.random_state == nullptr
                               ? fallback_random
                               : *ctx.random_state;
  const uint8_t fade_amount = static_cast<uint8_t>(
      10U + (static_cast<uint16_t>(255U - ctx.intensity) * 70U) / 255U);
  const uint8_t bpm = static_cast<uint8_t>(
      10U + (static_cast<uint16_t>(ctx.speed) * 80U) / 255U);

  switch (id) {
    case 0:
      fill(ctx, ctx.base);
      break;
    case 1:
    case 2: {
      const uint8_t low = id == 1 ? 10 : 20;
      const uint8_t high = id == 1 ? 255 : 200;
      const uint8_t amount = static_cast<uint8_t>(
          beat(bpm, low, high, ctx.now_ms));
      fill(ctx, Rgb{scale8(ctx.base.r, amount),
                    scale8(ctx.base.g, amount),
                    scale8(ctx.base.b, amount)});
      break;
    }
    case 3:
    case 4:
      fade_range(ctx, fade_amount);
      ctx.runtime->pos = static_cast<uint16_t>(
          (ctx.runtime->pos + step_from_speed(ctx.speed, id == 3 ? 32 : 24)) %
          ctx.count);
      ctx.pixels[ctx.start + ctx.runtime->pos] = ctx.base;
      break;
    case 5: {
      fade_range(ctx, fade_amount);
      const uint16_t pos = beat(bpm, 0, ctx.count - 1, ctx.now_ms);
      add(ctx.pixels[ctx.start + pos], ctx.base);
      break;
    }
    case 6: {
      fade_range(ctx, fade_amount);
      const uint16_t pos = random8(
          random_state, static_cast<uint8_t>(ctx.count - 1U));
      add(ctx.pixels[ctx.start + pos], ctx.base);
      break;
    }
    case 7:
      fade_range(ctx, fade_amount);
      for (uint8_t i = 0; i < 4; ++i) {
        const uint16_t pos = beat(static_cast<uint16_t>(bpm + i * 2U),
                                  0, ctx.count - 1, ctx.now_ms);
        add(ctx.pixels[ctx.start + pos], ctx.base);
      }
      break;
    case 8: {
      const uint8_t amount = static_cast<uint8_t>(
          beat(bpm, 64, 255, ctx.now_ms));
      fill(ctx, Rgb{scale8(ctx.base.r, amount),
                    scale8(ctx.base.g, amount),
                    scale8(ctx.base.b, amount)});
      break;
    }
    case 9:
      ctx.runtime->hue = static_cast<uint8_t>(
          ctx.runtime->hue + step_from_speed(ctx.speed, 16));
      for (uint16_t i = 0; i < ctx.count; ++i) {
        ctx.pixels[ctx.start + i] = hsv_to_rgb(
            static_cast<uint8_t>(ctx.runtime->hue +
                                 (ctx.start + i) * 7U), 255, 255);
      }
      break;
    case 10:
      if (ctx.heat != nullptr) render_fire(ctx, random_state);
      break;
    case 11:
      ctx.runtime->hue = static_cast<uint8_t>(
          ctx.runtime->hue + step_from_speed(ctx.speed, 24));
      for (uint16_t i = 0; i < ctx.count; ++i) {
        ctx.pixels[ctx.start + i] = hsv_to_rgb(
            static_cast<uint8_t>(ctx.runtime->hue +
                                 (ctx.start + i) * 8U), 200, 255);
      }
      break;
    default:
      fill(ctx, ctx.base);
      break;
  }
}

} // namespace led
