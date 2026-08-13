#include "led/led_compositor.h"

namespace led {

LedCompositor::LedCompositor(const LedLayoutConfig &layout_config)
    : layout_(layout_config),
      transition_from_{},
      transition_started_ms_(0),
      diagnostics_{false, 0, 255, 0, 0, 0} {}

void LedCompositor::clear_frame(LedFrame &frame) const {
  for (uint16_t i = 0; i < LED_STRIP_COUNT; ++i) {
    frame.bus_a[i] = Rgb{0, 0, 0};
    frame.bus_b[i] = Rgb{0, 0, 0};
  }
}

void LedCompositor::begin_transition(const LedFrame &visible, uint32_t now_ms,
                                     uint16_t duration_ms) {
  transition_from_ = visible;
  transition_started_ms_ = now_ms;
  diagnostics_.duration_ms = duration_ms;
  diagnostics_.progress = duration_ms == 0U ? 255U : 0U;
  diagnostics_.active = duration_ms != 0U;
  if (diagnostics_.active) {
    diagnostics_.started++;
  }
}

void LedCompositor::cancel_transition() {
  diagnostics_.active = false;
  diagnostics_.progress = 255;
}

void LedCompositor::interrupt_for_alert() {
  if (diagnostics_.active) {
    diagnostics_.active = false;
    diagnostics_.progress = 255;
    diagnostics_.interrupted++;
  }
}

uint8_t LedCompositor::transition_progress(uint32_t now_ms) const {
  if (!diagnostics_.active || diagnostics_.duration_ms == 0U) return 255;
  const uint32_t elapsed = now_ms - transition_started_ms_;
  if (elapsed >= diagnostics_.duration_ms) return 255;
  return static_cast<uint8_t>(
      (elapsed * 255UL) / diagnostics_.duration_ms);
}

void LedCompositor::map_target(const LedFrame &logical, LedFrame &physical,
                               bool mirror, bool status_enabled) {
  clear_frame(physical);
  layout_.set_mirror(mirror);
  const uint16_t status_count = layout_.status_pixels_per_bus();
  const uint16_t body_count = layout_.body_pixels_per_bus();
  LedAddress addresses[2] = {};

  if (mirror) {
    for (uint16_t i = 0; i < body_count; ++i) {
      const uint8_t count = layout_.map(LedRegion::BodyAll, i, addresses, 2);
      const Rgb color = logical.bus_a[status_count + i];
      for (uint8_t j = 0; j < count; ++j) {
        Rgb *bus = addresses[j].bus == LedBusId::A
                       ? physical.bus_a
                       : physical.bus_b;
        bus[addresses[j].physical_index] = color;
      }
    }
  } else {
    for (uint16_t i = 0; i < body_count; ++i) {
      uint8_t count = layout_.map(LedRegion::BodyLeft, i, addresses, 2);
      if (count > 0U) {
        physical.bus_a[addresses[0].physical_index] =
            logical.bus_a[status_count + i];
      }
      count = layout_.map(LedRegion::BodyRight, i, addresses, 2);
      if (count > 0U) {
        physical.bus_b[addresses[0].physical_index] =
            logical.bus_b[status_count + i];
      }
    }
  }

  if (!status_enabled) return;
  for (uint16_t i = 0; i < status_count; ++i) {
    const uint8_t count = layout_.map(LedRegion::Status, i, addresses, 2);
    for (uint8_t j = 0; j < count; ++j) {
      Rgb *target_bus = addresses[j].bus == LedBusId::A
                            ? physical.bus_a
                            : physical.bus_b;
      const Rgb *source_bus = addresses[j].bus == LedBusId::A
                                  ? logical.bus_a
                                  : logical.bus_b;
      target_bus[addresses[j].physical_index] = source_bus[i];
    }
  }
}

void LedCompositor::blend_transition_body(LedFrame &physical,
                                          uint8_t amount) const {
  const uint16_t body_count = layout_.body_pixels_per_bus();
  for (uint16_t i = 0; i < body_count; ++i) {
    const uint16_t a = layout_.physical_body_index(LedBusId::A, i);
    physical.bus_a[a] = blend_rgb(transition_from_.bus_a[a],
                                  physical.bus_a[a], amount);
    if (layout_.dual_bus()) {
      const uint16_t b = layout_.physical_body_index(LedBusId::B, i);
      physical.bus_b[b] = blend_rgb(transition_from_.bus_b[b],
                                    physical.bus_b[b], amount);
    }
  }
}

void LedCompositor::apply_alert(LedFrame &physical, const Rgb &color) const {
  LedAddress addresses[2] = {};
  for (uint16_t i = 0; i < layout_.region_size(LedRegion::Alert); ++i) {
    const uint8_t count = layout_.map(LedRegion::Alert, i, addresses, 2);
    for (uint8_t j = 0; j < count; ++j) {
      Rgb *bus = addresses[j].bus == LedBusId::A
                     ? physical.bus_a
                     : physical.bus_b;
      bus[addresses[j].physical_index] = color;
    }
  }
}

void LedCompositor::compose(const LedFrame &logical, LedFrame &physical,
                            uint32_t now_ms, bool mirror,
                            bool status_enabled, bool alert_active,
                            const Rgb &alert_color) {
  map_target(logical, physical, mirror, status_enabled);
  if (alert_active) {
    interrupt_for_alert();
  } else if (diagnostics_.active) {
    diagnostics_.progress = transition_progress(now_ms);
    blend_transition_body(physical, diagnostics_.progress);
    if (diagnostics_.progress == 255U) {
      diagnostics_.active = false;
      diagnostics_.completed++;
    }
  }
  if (alert_active) {
    apply_alert(physical, alert_color);
  }
}

const LedLayout &LedCompositor::layout() const {
  return layout_;
}

const TransitionDiagnostics &LedCompositor::diagnostics() const {
  return diagnostics_;
}

} // namespace led
