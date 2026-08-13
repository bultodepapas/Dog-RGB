#include "led/led_layout.h"

namespace led {

LedLayout::LedLayout(const LedLayoutConfig &config) : config_(config) {
  if (config_.status_pixels_per_bus > config_.pixels_per_bus) {
    config_.status_pixels_per_bus = config_.pixels_per_bus;
  }
  if (!config_.dual_bus) {
    config_.mirror = false;
  }
}

uint16_t LedLayout::pixels_per_bus() const {
  return config_.pixels_per_bus;
}

uint16_t LedLayout::status_pixels_per_bus() const {
  return config_.status_pixels_per_bus;
}

uint16_t LedLayout::body_pixels_per_bus() const {
  return static_cast<uint16_t>(config_.pixels_per_bus -
                               config_.status_pixels_per_bus);
}

bool LedLayout::dual_bus() const {
  return config_.dual_bus;
}

bool LedLayout::mirror() const {
  return config_.mirror;
}

void LedLayout::set_mirror(bool enabled) {
  config_.mirror = enabled && config_.dual_bus;
}

LedOrientation LedLayout::orientation(LedBusId bus) const {
  return bus == LedBusId::A ? config_.bus_a_orientation
                            : config_.bus_b_orientation;
}

uint16_t LedLayout::region_size(LedRegion region) const {
  switch (region) {
    case LedRegion::Status:
    case LedRegion::Alert:
      return status_pixels_per_bus();
    case LedRegion::BodyLeft:
      return body_pixels_per_bus();
    case LedRegion::BodyRight:
      return dual_bus() ? body_pixels_per_bus() : 0U;
    case LedRegion::BodyAll:
      return mirror() ? body_pixels_per_bus()
                      : static_cast<uint16_t>(body_pixels_per_bus() *
                                              (dual_bus() ? 2U : 1U));
  }
  return 0;
}

uint16_t LedLayout::physical_body_index(LedBusId bus,
                                        uint16_t logical_index) const {
  if (logical_index >= body_pixels_per_bus()) {
    return pixels_per_bus();
  }
  if (orientation(bus) == LedOrientation::Reverse) {
    return static_cast<uint16_t>(pixels_per_bus() - 1U - logical_index);
  }
  return static_cast<uint16_t>(status_pixels_per_bus() + logical_index);
}

uint16_t LedLayout::physical_full_index(LedBusId bus,
                                        uint16_t logical_index) const {
  if (logical_index >= pixels_per_bus()) {
    return pixels_per_bus();
  }
  return orientation(bus) == LedOrientation::Reverse
             ? static_cast<uint16_t>(pixels_per_bus() - 1U - logical_index)
             : logical_index;
}

uint8_t LedLayout::map(LedRegion region, uint16_t logical_index,
                       LedAddress *addresses, uint8_t capacity) const {
  if (addresses == nullptr || capacity == 0U ||
      logical_index >= region_size(region)) {
    return 0;
  }

  if (region == LedRegion::Status || region == LedRegion::Alert) {
    addresses[0] = LedAddress{LedBusId::A, logical_index};
    if (dual_bus() && capacity > 1U) {
      addresses[1] = LedAddress{LedBusId::B, logical_index};
      return 2;
    }
    return 1;
  }

  if (region == LedRegion::BodyLeft) {
    addresses[0] = LedAddress{
        LedBusId::A, physical_body_index(LedBusId::A, logical_index)};
    return 1;
  }

  if (region == LedRegion::BodyRight) {
    addresses[0] = LedAddress{
        LedBusId::B, physical_body_index(LedBusId::B, logical_index)};
    return 1;
  }

  if (mirror()) {
    addresses[0] = LedAddress{
        LedBusId::A, physical_body_index(LedBusId::A, logical_index)};
    if (dual_bus() && capacity > 1U) {
      addresses[1] = LedAddress{
          LedBusId::B, physical_body_index(LedBusId::B, logical_index)};
      return 2;
    }
    return 1;
  }

  const uint16_t body_count = body_pixels_per_bus();
  const bool on_second_bus = dual_bus() && logical_index >= body_count;
  const LedBusId bus = on_second_bus ? LedBusId::B : LedBusId::A;
  const uint16_t local_index =
      on_second_bus ? static_cast<uint16_t>(logical_index - body_count)
                    : logical_index;
  addresses[0] = LedAddress{bus, physical_body_index(bus, local_index)};
  return 1;
}

const char *led_region_name(LedRegion region) {
  switch (region) {
    case LedRegion::Status: return "status";
    case LedRegion::BodyLeft: return "body_left";
    case LedRegion::BodyRight: return "body_right";
    case LedRegion::BodyAll: return "body_all";
    case LedRegion::Alert: return "alert";
  }
  return "unknown";
}

const char *led_orientation_name(LedOrientation orientation) {
  return orientation == LedOrientation::Reverse ? "reverse" : "forward";
}

} // namespace led
