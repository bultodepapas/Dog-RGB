#pragma once

#if (defined(DOG_RGB_BOARD_XIAO_S3) + defined(DOG_RGB_BOARD_WAVESHARE_LCD169_V2)) != 1
#error "Select exactly one supported Dog-RGB board profile"
#endif

#if defined(DOG_RGB_BOARD_XIAO_S3)
#if DOG_RGB_BOARD_XIAO_S3 != 1
#error "DOG_RGB_BOARD_XIAO_S3 must be 1"
#endif
#include "board/xiao_s3.h"
#else
#if DOG_RGB_BOARD_WAVESHARE_LCD169_V2 != 1
#error "DOG_RGB_BOARD_WAVESHARE_LCD169_V2 must be 1"
#endif
#include "board/waveshare_lcd169_v2.h"
#endif

#if defined(DOG_RGB_WOKWI_SIM) && !defined(DOG_RGB_BOARD_XIAO_S3)
#error "Wokwi is a Classic-only target"
#endif

#if defined(DOG_RGB_BRINGUP_STAGE)
#if !defined(DOG_RGB_BOARD_WAVESHARE_LCD169_V2) || DOG_RGB_BRINGUP_STAGE < 0 || DOG_RGB_BRINGUP_STAGE > 1
#error "Only Waveshare bringup stages 0 and 1 are implemented"
#endif
#endif

namespace board {
constexpr bool is_external_pin_available(int pin) {
  if (pin < 0 || pin > 48 || (pin >= 22 && pin <= 25)) {
    return false;
  }
  for (int reserved : kReservedPins) {
    if (pin == reserved) {
      return false;
    }
  }
  return true;
}

constexpr bool external_pins_valid() {
  const int pins[] = {kLedAData, kLedBData, kGpsRx, kGpsTx, kStatusLedPin};
  for (unsigned i = 0; i < 5; ++i) {
    if (i == 4 && pins[i] == -1) {
      continue;
    }
    if (!is_external_pin_available(pins[i])) {
      return false;
    }
    for (unsigned j = 0; j < i; ++j) {
      if (pins[i] == pins[j]) {
        return false;
      }
    }
  }
  return true;
}
static_assert(external_pins_valid(), "Collar pins collide or use reserved GPIOs");
} // namespace board
