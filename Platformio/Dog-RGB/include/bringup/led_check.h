#pragma once

#include <stdint.h>

namespace bringup::led_check {
constexpr uint8_t kBrightness = 16;
constexpr uint32_t kStepMs = 2000;
constexpr uint8_t kSteps = 15; // A, B, both; each R/G/B/W/off.
constexpr uint32_t kOnePixelMs = kSteps * kStepMs;
constexpr uint32_t kFullTestMs = 15UL * 60 * 1000;

void begin();
// Input is read without waiting for a whole line. '0' wins over other commands
// in the same bounded batch. USB loss stops output; reconnect never resumes it.
void tick(uint32_t now_ms);
void report();
} // namespace bringup::led_check
