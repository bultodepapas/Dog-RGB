#pragma once

#include <stdint.h>

namespace led {

enum class LedBusId : uint8_t {
  A = 0,
  B = 1,
};

enum class LedOrientation : uint8_t {
  Forward = 0,
  Reverse = 1,
};

enum class LedRegion : uint8_t {
  Status = 0,
  BodyLeft = 1,
  BodyRight = 2,
  BodyAll = 3,
  Alert = 4,
};

struct LedAddress {
  LedBusId bus;
  uint16_t physical_index;
};

struct LedLayoutConfig {
  uint16_t pixels_per_bus;
  uint16_t status_pixels_per_bus;
  bool dual_bus;
  LedOrientation bus_a_orientation;
  LedOrientation bus_b_orientation;
  bool mirror;
};

class LedLayout {
 public:
  explicit LedLayout(const LedLayoutConfig &config);

  uint16_t pixels_per_bus() const;
  uint16_t status_pixels_per_bus() const;
  uint16_t body_pixels_per_bus() const;
  bool dual_bus() const;
  bool mirror() const;
  void set_mirror(bool enabled);
  LedOrientation orientation(LedBusId bus) const;

  uint16_t region_size(LedRegion region) const;
  uint8_t map(LedRegion region, uint16_t logical_index,
              LedAddress *addresses, uint8_t capacity) const;
  uint16_t physical_body_index(LedBusId bus,
                               uint16_t logical_index) const;
  uint16_t physical_full_index(LedBusId bus,
                               uint16_t logical_index) const;

 private:
  LedLayoutConfig config_;
};

const char *led_region_name(LedRegion region);
const char *led_orientation_name(LedOrientation orientation);

} // namespace led
