#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "led/effect_registry.h"
#include "led/led_color.h"
#include "led/led_compositor.h"
#include "led/led_layout.h"
#include "led/palette_registry.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

bool equal(const led::Rgb &left, const led::Rgb &right) {
  return left.r == right.r && left.g == right.g && left.b == right.b;
}

bool equal(const led::Rgbw &left, const led::Rgbw &right) {
  return left.r == right.r && left.g == right.g && left.b == right.b &&
         left.w == right.w;
}

led::LedLayoutConfig test_layout(bool mirror = true) {
  return led::LedLayoutConfig{8, 2, true, led::LedOrientation::Forward,
                              led::LedOrientation::Reverse, mirror};
}

void test_layout_contract() {
  led::LedLayout layout(test_layout());
  led::LedAddress addresses[2]{};

  expect(layout.status_pixels_per_bus() == 2 &&
             layout.body_pixels_per_bus() == 6,
         "semantic region sizes changed");
  expect(layout.region_size(led::LedRegion::BodyAll) == 6,
         "mirrored body_all must expose one virtual branch");
  expect(layout.physical_body_index(led::LedBusId::A, 0) == 2 &&
             layout.physical_body_index(led::LedBusId::A, 5) == 7,
         "forward branch orientation is wrong");
  expect(layout.physical_body_index(led::LedBusId::B, 0) == 7 &&
             layout.physical_body_index(led::LedBusId::B, 5) == 2,
         "reverse branch orientation is wrong");

  uint8_t count = layout.map(led::LedRegion::Status, 1, addresses, 2);
  expect(count == 2 && addresses[0].physical_index == 1 &&
             addresses[1].physical_index == 1,
         "status region must remain fixed on both buses");
  count = layout.map(led::LedRegion::BodyAll, 0, addresses, 2);
  expect(count == 2 && addresses[0].bus == led::LedBusId::A &&
             addresses[0].physical_index == 2 &&
             addresses[1].bus == led::LedBusId::B &&
             addresses[1].physical_index == 7,
         "mirror mapping is not physically symmetric");

  layout.set_mirror(false);
  expect(layout.region_size(led::LedRegion::BodyAll) == 12,
         "independent body_all must expose both branches");
  count = layout.map(led::LedRegion::BodyAll, 6, addresses, 2);
  expect(count == 1 && addresses[0].bus == led::LedBusId::B &&
             addresses[0].physical_index == 7,
         "continuous body_all did not cross to the second branch");
  expect(layout.map(led::LedRegion::BodyLeft, 6, addresses, 2) == 0,
         "out-of-range semantic index must be rejected");
}

void test_palette_contract() {
  static const char *const KEYS[led::PALETTE_REGISTRY_COUNT] = {
      "safety_amber", "night_red", "ocean", "forest",
      "pride", "heat", "ice", "custom_ab"};
  expect(led::PALETTE_REGISTRY_VERSION == 1,
         "unexpected palette registry version");
  expect(led::palette_descriptor_count() == led::PALETTE_REGISTRY_COUNT,
         "palette count differs from its public contract");
  expect(!led::palette_id_valid(-1) &&
             !led::palette_id_valid(led::PALETTE_REGISTRY_COUNT),
         "invalid palette ids must be rejected");

  for (uint8_t id = 0; id < led::PALETTE_REGISTRY_COUNT; ++id) {
    const led::PaletteDescriptor *palette = led::palette_descriptor(id);
    expect(palette != nullptr && palette->id == id,
           "registered palette is not addressable by id");
    if (palette == nullptr) continue;
    expect(std::strcmp(palette->key, KEYS[id]) == 0,
           "stable palette key changed");
    expect(led::palette_descriptor_by_key(KEYS[id]) == palette,
           "palette key lookup is not canonical");
    expect(palette->stop_count >= 2 &&
               palette->stop_count <= led::PALETTE_MAX_STOPS,
           "palette stop count is unsafe");
    for (uint8_t stop = 0; stop < palette->stop_count; ++stop) {
      const led::Rgbw physical = palette->stops[stop];
      expect(equal(led::rgb_to_rgbw(led::rgbw_to_rgb(physical)), physical),
             "palette stop is not canonical RGBW");
    }
    const uint8_t positions[] = {0, 17, 63, 127, 191, 254, 255};
    for (uint8_t position : positions) {
      const led::Rgbw sampled = led::palette_sample_rgbw(
          id, position, led::Rgb{0, 140, 200}, led::Rgb{180, 0, 120});
      expect(equal(led::rgb_to_rgbw(led::rgbw_to_rgb(sampled)), sampled),
             "interpolated palette sample is not canonical RGBW");
    }
  }

  const led::Rgb custom_a{90, 60, 30};
  const led::Rgb custom_b{20, 80, 140};
  expect(equal(led::palette_sample_rgb(led::PALETTE_CUSTOM_AB, 0,
                                       custom_a, custom_b),
               custom_a),
         "custom palette does not begin at color A");
  expect(equal(led::palette_sample_rgb(led::PALETTE_CUSTOM_AB, 255,
                                       custom_a, custom_b),
               custom_b),
         "custom palette does not end at color B");
  const led::Rgbw warm_white = led::palette_sample_rgbw(
      led::PALETTE_SAFETY_AMBER, 192, custom_a, custom_b);
  expect(warm_white.w > 0,
         "curated RGBW palettes never exercise the white channel");
}

std::array<led::Rgb, 16> render_once(uint8_t effect_id,
                                     uint8_t palette_id) {
  std::array<led::Rgb, 16> pixels{};
  std::array<uint8_t, 16> heat{};
  uint32_t random_state = UINT32_C(0x12345678);
  led::EffectRuntime runtime{31, 2};
  led::EffectRenderContext context{
      pixels.data(), heat.data(), 2, 12, {120, 40, 10}, {5, 80, 150},
      palette_id, 137, 173, 733, &random_state, &runtime};
  led::render_effect(effect_id, context);
  return pixels;
}

void test_palette_aware_effects() {
  const uint8_t effect_ids[] = {2, 3, 4, 9, 11};
  const uint8_t palette_ids[] = {
      led::PALETTE_CUSTOM_AB, led::PALETTE_OCEAN, led::PALETTE_FOREST,
      led::PALETTE_PRIDE, led::PALETTE_ICE};
  for (size_t i = 0; i < sizeof(effect_ids) / sizeof(effect_ids[0]); ++i) {
    const auto legacy = render_once(effect_ids[i], led::PALETTE_NONE);
    const auto palette = render_once(effect_ids[i], palette_ids[i]);
    expect(std::memcmp(legacy.data(), palette.data(),
                       legacy.size() * sizeof(led::Rgb)) != 0,
           "palette-aware effect ignored its selected palette");
    expect(equal(palette[0], led::Rgb{0, 0, 0}) &&
               equal(palette[1], led::Rgb{0, 0, 0}) &&
               equal(palette[14], led::Rgb{0, 0, 0}) &&
               equal(palette[15], led::Rgb{0, 0, 0}),
           "palette-aware effect escaped its semantic body segment");
  }
}

void test_compositor_transition_and_alert() {
  led::LedCompositor compositor(test_layout());
  led::LedFrame visible{};
  led::LedFrame logical{};
  led::LedFrame physical{};

  for (uint16_t i = 0; i < 8; ++i) {
    visible.bus_a[i] = led::Rgb{100, 0, 0};
    visible.bus_b[i] = led::Rgb{100, 0, 0};
  }
  logical.bus_a[0] = led::Rgb{0, 40, 0};
  logical.bus_b[0] = led::Rgb{0, 40, 0};
  logical.bus_a[1] = led::Rgb{0, 0, 40};
  logical.bus_b[1] = led::Rgb{0, 0, 40};
  for (uint16_t i = 2; i < 8; ++i) {
    logical.bus_a[i] = led::Rgb{0, 0, 100};
    logical.bus_b[i] = led::Rgb{50, 50, 0};
  }

  compositor.begin_transition(visible, 1000, 100);
  compositor.compose(logical, physical, 1000, true, true, false,
                     led::Rgb{0, 0, 0});
  expect(equal(physical.bus_a[2], led::Rgb{100, 0, 0}) &&
             equal(physical.bus_b[7], led::Rgb{100, 0, 0}),
         "crossfade did not begin from the visible frame");
  expect(equal(physical.bus_a[0], led::Rgb{0, 40, 0}) &&
             equal(physical.bus_b[1], led::Rgb{0, 0, 40}),
         "status LEDs were faded with the decorative body");

  compositor.compose(logical, physical, 1050, true, true, false,
                     led::Rgb{0, 0, 0});
  expect(physical.bus_a[2].r > 0 && physical.bus_a[2].b > 0,
         "crossfade inserted a black frame between non-black effects");
  expect(equal(physical.bus_a[2], physical.bus_b[7]),
         "mirror did not duplicate one logical branch after orientation");

  compositor.compose(logical, physical, 1100, true, true, false,
                     led::Rgb{0, 0, 0});
  expect(equal(physical.bus_a[2], led::Rgb{0, 0, 100}) &&
             !compositor.diagnostics().active &&
             compositor.diagnostics().completed == 1,
         "crossfade did not complete at its declared duration");

  compositor.begin_transition(physical, 2000, 500);
  const led::Rgb alert{180, 0, 0};
  compositor.compose(logical, physical, 2050, true, true, true, alert);
  expect(equal(physical.bus_a[0], alert) &&
             equal(physical.bus_a[1], alert) &&
             equal(physical.bus_b[0], alert) &&
             equal(physical.bus_b[1], alert),
         "alert overlay was not visible on the next composed frame");
  expect(!compositor.diagnostics().active &&
             compositor.diagnostics().interrupted == 1,
         "alert did not interrupt the decorative transition");
  expect(equal(physical.bus_a[2], led::Rgb{0, 0, 100}),
         "status alert destroyed the decorative body");

  compositor.cancel_transition();
  compositor.compose(logical, physical, 3000, true, true, false,
                     led::Rgb{0, 0, 0}, 128);
  expect(equal(physical.bus_a[0], led::Rgb{0, 40, 0}) &&
             equal(physical.bus_b[1], led::Rgb{0, 0, 40}),
         "scene body level scaled status pixels");
  expect(physical.bus_a[2].b >= 49 && physical.bus_a[2].b <= 51,
         "scene body level was not applied before physical output");
  compositor.compose(logical, physical, 3050, true, true, true, alert, 32);
  expect(equal(physical.bus_a[0], alert) && equal(physical.bus_b[1], alert),
         "scene body level reduced the safety alert overlay");
}

}  // namespace

int main() {
  test_layout_contract();
  test_palette_contract();
  test_palette_aware_effects();
  test_compositor_transition_and_alert();
  if (failures != 0) {
    std::fprintf(stderr, "led_phase3_characterization: %d failure(s)\n",
                 failures);
    return 1;
  }
  std::puts("led_phase3_characterization: ok");
  return 0;
}
