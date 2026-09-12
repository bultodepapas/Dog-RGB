#include "bringup/led_check.h"

#include <Arduino.h>
#include "board/board_profile.h"
#include "led/led_bus.h"

#if !defined(DOG_RGB_BRINGUP_STAGE) || DOG_RGB_BRINGUP_STAGE != 1
#error "LED check belongs only to bringup stage 1"
#endif

namespace bringup::led_check {
namespace {
// Same driver/conversion/limiter as the product; led_ui.cpp is not linked here.
led::LedBus bus(LED_STRIP_COUNT, board::kLedAData, board::kLedBData, true);
led::LedFrame frame{};
bool running = false;
bool full_strip = false;
bool one_pixel_complete = false;
bool usb_was_connected = false;
bool drain_on_connect = true;
uint16_t visited_steps = 0;
uint8_t last_step = kSteps;
uint32_t started_ms = 0;
constexpr led::Rgb kColors[] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}};
constexpr const char *kColorNames[] = {"red", "green", "blue", "white-W", "off"};
constexpr const char *kBusNames[] = {"A", "B", "AB"};
static_assert(kBrightness <= 16, "Bench brightness must remain capped");
static_assert(LED_STRIP_MODE == 2, "I1 requires two independently wired strips");

void stop() {
  running = false;
  last_step = kSteps;
  frame = {};
  bus.show(frame); // Actually clear the latched pixels; do not just stop ticks.
}

void start(bool full, uint32_t now_ms) {
  if (full && !one_pixel_complete) {
    Serial.printf("[I1] full refused: finish '1' first, then inspect both strips\n");
    return;
  }
  if (!full) one_pixel_complete = false;
  full_strip = full;
  started_ms = now_ms;
  visited_steps = 0;
  last_step = kSteps;
  running = true;
}

void render_step(uint8_t step) {
  frame = {};
  const unsigned count = full_strip ? LED_STRIP_COUNT : 1;
  const uint8_t group = step / 5;
  const led::Rgb color = kColors[step % 5];
  for (unsigned i = 0; i < count; ++i) {
    if (group != 1) frame.bus_a[i] = color;
    if (group != 0) frame.bus_b[i] = color;
  }
  bus.show(frame);
  last_step = step;
  visited_steps |= static_cast<uint16_t>(1U << step);
}
} // namespace

void begin() {
  // RAM-only bench configuration; persisted product settings are never read
  // or changed. Keep the existing estimator enabled, not a raw NeoPixel path.
  const led::PowerLimitConfig power{
      true, LED_POWER_BUDGET_MA_DEFAULT, LED_BASE_CURRENT_MA_DEFAULT,
      LED_RGB_CHANNEL_MA_DEFAULT, LED_WHITE_CHANNEL_MA_DEFAULT};
  bus.configure_power(power);
  bus.begin(kBrightness);
  one_pixel_complete = false;
  full_strip = false;
  usb_was_connected = false;
  drain_on_connect = true;
  stop();
}

void tick(uint32_t now_ms) {
  const bool connected = static_cast<bool>(Serial);
  if (!connected) {
    if (running) stop();
    usb_was_connected = false;
    drain_on_connect = true;
    return;
  }
  if (!usb_was_connected) {
    usb_was_connected = true;
    drain_on_connect = true;
  }
  // Discard old queued commands on (re)connection without an unbounded loop.
  if (drain_on_connect) {
    for (unsigned i = 0; i < 16 && Serial.available() > 0; ++i) Serial.read();
    if (Serial.available() == 0) {
      drain_on_connect = false;
      Serial.printf("[I1] ready: 1=one-pixel 30s, f=full 15min, 0=off, ?=status; brightness=16\n");
    }
    return;
  }

  char command = 0;
  bool off_requested = false;
  bool status_requested = false;
  for (unsigned i = 0; i < 16 && Serial.available() > 0; ++i) {
    const int value = Serial.read();
    if (value == '0') off_requested = true;
    else if (value == '1' || value == 'f') command = static_cast<char>(value);
    else if (value == '?') status_requested = true;
  }
  if (off_requested) {
    stop();
    // Discard anything queued behind stop; a new command follows the ready line.
    drain_on_connect = true;
    return;
  }
  if (command) start(command == 'f', now_ms);

  if (running) {
    const uint32_t elapsed = now_ms - started_ms;
    const uint32_t duration = full_strip ? kFullTestMs : kOnePixelMs;
    if (elapsed >= duration) {
      if (!full_strip) {
        // A large stalled tick must not certify steps that were never sent.
        one_pixel_complete = visited_steps == static_cast<uint16_t>((1U << kSteps) - 1U);
      }
      stop();
    } else {
      const uint8_t step = (elapsed / kStepMs) % kSteps;
      if (step != last_step) render_step(step); // No burst/catch-up traffic.
    }
  }
  if (status_requested) report();
}

void report() {
  if (!Serial) return;
  const led::PowerDiagnostics &power = bus.power_diagnostics();
  Serial.printf("[I1] running=%d scope=%s pixels=%d bus=%s color=%s "
                "one_pixel_complete=%d brightness=%u budget_ma=%u "
                "requested_ma=%u estimated_ma=%u scale=%u limited_frames=%lu\n",
                running, full_strip ? "full" : "one", LED_STRIP_COUNT,
                running ? kBusNames[last_step / 5] : "none",
                running ? kColorNames[last_step % 5] : "off",
                one_pixel_complete, kBrightness, LED_POWER_BUDGET_MA_DEFAULT,
                power.requested_ma, power.estimated_ma, power.scale,
                static_cast<unsigned long>(power.frames_limited));
}
} // namespace bringup::led_check
