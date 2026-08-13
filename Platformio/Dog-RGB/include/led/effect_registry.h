#pragma once

#include <stddef.h>
#include <stdint.h>

#include "led/led_color.h"

namespace led {

static const uint8_t EFFECT_REGISTRY_VERSION = 1;
static const uint8_t EFFECT_REGISTRY_COUNT = 12;

enum EffectControl : uint8_t {
  EFFECT_CONTROL_NONE = 0,
  EFFECT_CONTROL_SPEED = 1 << 0,
  EFFECT_CONTROL_INTENSITY = 1 << 1,
  EFFECT_CONTROL_COLOR = 1 << 2,
};

enum class EffectColorMode : uint8_t {
  Base = 0,
  Generated = 1,
};

enum class EffectPaletteMode : uint8_t {
  None = 0,
  Internal = 1,
  Selectable = 2,
};

enum class EffectSafetyClass : uint8_t {
  Calm = 0,
  Active = 1,
  Advanced = 2,
};

struct EffectDefaults {
  uint8_t speed;
  uint8_t intensity;
};

struct EffectUsefulRange {
  uint8_t speed_min;
  uint8_t speed_max;
  uint8_t intensity_min;
  uint8_t intensity_max;
};

struct EffectDescriptor {
  uint8_t id;
  const char *key;
  const char *label;
  uint8_t controls;
  EffectDefaults defaults;
  EffectUsefulRange useful;
  EffectColorMode color_mode;
  EffectPaletteMode palette_mode;
  EffectSafetyClass safety;
};

struct EffectRuntime {
  uint8_t hue;
  uint16_t pos;
};

struct EffectRenderContext {
  Rgb *pixels;
  uint8_t *heat;
  uint16_t start;
  uint16_t count;
  Rgb base;
  uint8_t speed;
  uint8_t intensity;
  uint32_t now_ms;
  uint32_t *random_state;
  EffectRuntime *runtime;
};

const EffectDescriptor *effect_descriptor(uint8_t id);
const EffectDescriptor *effect_descriptor_by_key(const char *key);
const EffectDescriptor &effect_descriptor_at(size_t index);
size_t effect_descriptor_count();
bool effect_id_valid(int32_t id);
const char *effect_safety_name(EffectSafetyClass safety);
const char *effect_color_mode_name(EffectColorMode mode);
const char *effect_palette_mode_name(EffectPaletteMode mode);

// Pure renderer: no Arduino, GPS, Wi-Fi, NVS or allocation. Time and random
// state are explicit so native characterization can replay every effect.
void render_effect(uint8_t id, EffectRenderContext &context);

} // namespace led
