#pragma once

namespace board {
constexpr const char *kId = "classic-xiao-s3";
constexpr const char *kRevision = "xiao-s3";
constexpr bool kHasDisplay = false;
constexpr bool kHasTouch = false;
constexpr int kStatusLedPin = 3;
constexpr int kLedAData = 1;
constexpr int kLedBData = 2;
constexpr int kGpsRx = 44;
constexpr int kGpsTx = 43;
constexpr int kPowerHoldPin = -1;
constexpr int kPowerButtonPin = -1;
constexpr int kBacklightPin = -1;
constexpr unsigned long kFlashBytes = 8UL * 1024 * 1024;
constexpr unsigned long kPsramBytes = 8UL * 1024 * 1024;
// Native USB and flash/OPI PSRAM pins are not external collar outputs.
constexpr int kReservedPins[] = {
    19, 20, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37};
} // namespace board
