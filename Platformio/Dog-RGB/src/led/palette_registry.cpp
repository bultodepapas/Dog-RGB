#include "led/palette_registry.h"

#include <string.h>

namespace led {
namespace {

static const PaletteDescriptor PALETTES[PALETTE_REGISTRY_COUNT] = {
    {PALETTE_SAFETY_AMBER, "safety_amber", "Safety Amber", 4, true,
     {{180, 45, 0, 0}, {200, 70, 0, 20}, {120, 25, 0, 70},
      {0, 0, 0, 180}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    {PALETTE_NIGHT_RED, "night_red", "Night Red", 4, true,
     {{18, 0, 0, 0}, {55, 0, 0, 0}, {100, 0, 0, 0},
      {30, 0, 0, 8}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    {PALETTE_OCEAN, "ocean", "Ocean", 4, true,
     {{0, 15, 100, 0}, {0, 100, 150, 0}, {0, 150, 170, 20},
      {0, 20, 80, 120}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    {PALETTE_FOREST, "forest", "Forest", 4, true,
     {{0, 45, 5, 0}, {0, 120, 55, 0}, {0, 150, 90, 20},
      {0, 40, 20, 100}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    {PALETTE_PRIDE, "pride", "Pride", 6, true,
     {{220, 0, 0, 0}, {220, 70, 0, 0}, {180, 180, 0, 0},
      {0, 190, 20, 0}, {0, 50, 220, 0}, {140, 0, 200, 0}}},
    {PALETTE_HEAT, "heat", "Heat", 5, false,
     {{80, 0, 0, 0}, {200, 20, 0, 0}, {220, 90, 0, 0},
      {180, 180, 0, 30}, {0, 0, 0, 180}, {0, 0, 0, 0}}},
    {PALETTE_ICE, "ice", "Ice", 4, true,
     {{0, 15, 70, 40}, {0, 80, 150, 50}, {0, 20, 80, 140},
      {0, 0, 0, 210}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    {PALETTE_CUSTOM_AB, "custom_ab", "Custom A-B", 2, false,
     {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
      {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
};

static Rgbw sample_stops(const Rgbw *stops, uint8_t count, bool cyclic,
                         uint8_t position) {
  if (count == 0U) return Rgbw{0, 0, 0, 0};
  if (count == 1U) return stops[0];
  if (!cyclic && position == 255U) return stops[count - 1U];

  const uint8_t segment_count = cyclic ? count : static_cast<uint8_t>(count - 1U);
  const uint16_t scaled = static_cast<uint16_t>(position) * segment_count;
  uint8_t index = static_cast<uint8_t>(scaled >> 8U);
  if (index >= segment_count) index = static_cast<uint8_t>(segment_count - 1U);
  const uint8_t next = cyclic
                           ? static_cast<uint8_t>((index + 1U) % count)
                           : static_cast<uint8_t>(index + 1U);
  return blend_rgbw(stops[index], stops[next],
                    static_cast<uint8_t>(scaled & 0xFFU));
}

} // namespace

const PaletteDescriptor *palette_descriptor(uint8_t id) {
  return id < PALETTE_REGISTRY_COUNT ? &PALETTES[id] : nullptr;
}

const PaletteDescriptor *palette_descriptor_by_key(const char *key) {
  if (key == nullptr) return nullptr;
  for (size_t i = 0; i < PALETTE_REGISTRY_COUNT; ++i) {
    if (strcmp(PALETTES[i].key, key) == 0) return &PALETTES[i];
  }
  return nullptr;
}

const PaletteDescriptor &palette_descriptor_at(size_t index) {
  return PALETTES[index < PALETTE_REGISTRY_COUNT ? index : 0U];
}

size_t palette_descriptor_count() {
  return PALETTE_REGISTRY_COUNT;
}

bool palette_id_valid(int32_t id) {
  return id >= 0 && id < PALETTE_REGISTRY_COUNT;
}

Rgbw palette_sample_rgbw(uint8_t id, uint8_t position, const Rgb &custom_a,
                         const Rgb &custom_b) {
  Rgbw sampled{};
  if (id == PALETTE_CUSTOM_AB || !palette_id_valid(id)) {
    const Rgbw custom_stops[2] = {rgb_to_rgbw(custom_a),
                                  rgb_to_rgbw(custom_b)};
    sampled = sample_stops(custom_stops, 2, false, position);
  } else {
    const PaletteDescriptor &palette = PALETTES[id];
    sampled = sample_stops(palette.stops, palette.stop_count, palette.cyclic,
                           position);
  }
  // Interpolation between individually canonical stops can temporarily leave
  // every residual RGB channel non-zero. Canonicalize the sample so this API,
  // the power model and the bus all observe the exact same physical RGBW.
  return rgb_to_rgbw(rgbw_to_rgb(sampled));
}

Rgb palette_sample_rgb(uint8_t id, uint8_t position, const Rgb &custom_a,
                       const Rgb &custom_b) {
  return rgbw_to_rgb(palette_sample_rgbw(id, position, custom_a, custom_b));
}

} // namespace led
