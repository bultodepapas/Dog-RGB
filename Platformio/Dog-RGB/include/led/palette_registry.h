#pragma once

#include <stddef.h>
#include <stdint.h>

#include "led/led_color.h"

namespace led {

static const uint8_t PALETTE_REGISTRY_VERSION = 1;
static const uint8_t PALETTE_REGISTRY_COUNT = 8;
static const uint8_t PALETTE_MAX_STOPS = 6;
static const uint8_t PALETTE_NONE = 255;

enum PaletteId : uint8_t {
  PALETTE_SAFETY_AMBER = 0,
  PALETTE_NIGHT_RED = 1,
  PALETTE_OCEAN = 2,
  PALETTE_FOREST = 3,
  PALETTE_PRIDE = 4,
  PALETTE_HEAT = 5,
  PALETTE_ICE = 6,
  PALETTE_CUSTOM_AB = 7,
};

struct PaletteDescriptor {
  uint8_t id;
  const char *key;
  const char *label;
  uint8_t stop_count;
  bool cyclic;
  Rgbw stops[PALETTE_MAX_STOPS];
};

const PaletteDescriptor *palette_descriptor(uint8_t id);
const PaletteDescriptor *palette_descriptor_by_key(const char *key);
const PaletteDescriptor &palette_descriptor_at(size_t index);
size_t palette_descriptor_count();
bool palette_id_valid(int32_t id);

// Samples in physical RGBW, then converts through the same canonical path used
// by the bus. CUSTOM_AB derives its two endpoints from the current LED state.
Rgbw palette_sample_rgbw(uint8_t id, uint8_t position, const Rgb &custom_a,
                         const Rgb &custom_b);
Rgb palette_sample_rgb(uint8_t id, uint8_t position, const Rgb &custom_a,
                       const Rgb &custom_b);

} // namespace led
