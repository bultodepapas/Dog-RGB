#include "display/display.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <stdio.h>
#include <string.h>
#include "board/board_profile.h"
#include "display/text_view.h"

#if DOG_RGB_DISPLAY_ENABLED != 1
#error "Display sources must be excluded from display-free targets"
#endif

namespace display {
namespace {
constexpr uint16_t kBackground = 0x0842, kWhite = 0xEF7D, kMuted = 0x94D3;
constexpr uint32_t kSampleMs = 1000;
Arduino_ESP32SPI bus(board::kLcdDc, board::kLcdCs, board::kLcdSck,
                    board::kLcdMosi, GFX_NOT_DEFINED);
Arduino_ST7789 panel(&bus, board::kLcdReset, 0, true, board::kLcdWidth,
                    board::kLcdHeight, 0, board::kLcdRowOffset, 0, 0);
bool attempted = false, ready = false, enabled = true, backlight = false;
bool test_pattern = false, redraw = false;
uint32_t last_sample_ms = 0, sample_ms = 0, init_us = 0, max_tick_us = 0;
uint32_t max_service_us = 0;
uint32_t draw_ticks = 0, rows_drawn = 0;
// Upper bounds for active drawing ticks; idle calls never bias the percentile.
constexpr uint32_t kBuckets[] = {1000, 5000, 10000, 20000, 50000};
uint32_t histogram[6] = {};
TextView shown, pending;
uint8_t dirty = 0;

void light(bool on) {
  backlight = ready && enabled && on;
  digitalWrite(board::kBacklightPin, backlight ? HIGH : LOW);
}
void text(int y, const char *value, uint8_t size, uint16_t color) {
  panel.setTextSize(size);
  panel.setTextColor(color, kBackground);
  panel.setCursor(24, y);
  panel.print(value);
}
void frame() {
  panel.fillScreen(kBackground);
  panel.setTextWrap(false);
  text(22, "RGB DOG", 2, kWhite);
  panel.drawFastHLine(24, 48, 192, kMuted);
  text(57, "GPS", 1, kMuted);
  text(137, "km/h", 1, kMuted);
  text(157, "DIST. DIA REGISTRADO", 1, kMuted);
}
void row(uint8_t index) {
  constexpr int y[] = {73, 101, 174, 200, 232};
  constexpr uint8_t size[] = {2, 4, 2, 1, 2};
  panel.fillRect(24, y[index], 192, 8 * size[index], kBackground);
  text(y[index], pending.rows[index], size[index], index == 0 ? pending.gps_color : kWhite);
  memcpy(shown.rows[index], pending.rows[index], sizeof(shown.rows[index]));
  if (index == 0) shown.gps_color = pending.gps_color;
  ++rows_drawn;
}
void bars() {
  panel.fillScreen(kBackground);
  panel.drawRect(0, 0, board::kLcdWidth, board::kLcdHeight, kWhite);
  const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
  for (int i = 0; i < 4; ++i) panel.fillRect(24 + 48 * i, 80, 48, 100, colors[i]);
  text(24, "ARRIBA", 2, kWhite);
  text(200, "R   G   B   W", 2, kWhite);
  text(232, "240 x 280", 2, kWhite);
}
void record_draw(uint32_t elapsed) {
  ++draw_ticks;
  if (elapsed > max_tick_us) max_tick_us = elapsed;
  unsigned bucket = 0;
  while (bucket < 5 && elapsed > kBuckets[bucket]) ++bucket;
  ++histogram[bucket];
}
uint32_t p95_upper_us() {
  if (draw_ticks == 0) return 0;
  const uint32_t rank = static_cast<uint32_t>((static_cast<uint64_t>(draw_ticks) * 95 + 99) / 100);
  uint32_t sum = 0;
  for (unsigned i = 0; i < 6; ++i) {
    sum += histogram[i];
    if (sum >= rank) return i < 5 ? kBuckets[i] : max_tick_us;
  }
  return max_tick_us;
}
#if DOG_RGB_BRINGUP_STAGE == 3
void commands() {
  // Bounded USB input. Commands change only the LCD service, never GPS/LEDs/NVS.
  for (unsigned n = 0; n < 8 && Serial.available() > 0; ++n) {
    switch (Serial.read()) {
      case 't': test_pattern = true; redraw = true; break;
      case 'v': test_pattern = false; redraw = true; break;
      case 'b': light(!backlight); break;
      case 'd':
        enabled = !enabled;
        light(false);
        if (enabled) redraw = true;
        break;
      case 'r':
        draw_ticks = max_tick_us = rows_drawn = max_service_us = 0;
        memset(histogram, 0, sizeof(histogram));
        break;
      default: break;
    }
  }
}
#endif
} // namespace

bool begin() {
  if (attempted) return ready;
  attempted = true;
  const uint32_t started = micros();
  light(false);
  ready = panel.begin(board::kLcdSpiHz);
  if (ready) {
    frame();
    const DisplaySnapshot sample = capture_snapshot();
    sample_ms = last_sample_ms = sample.captured_ms;
    pending = format_view(sample);
    for (uint8_t i = 0; i < kRowCount; ++i) row(i);
    light(true); // Only after the first complete frame, never before init.
  }
  init_us = micros() - started;
  return ready;
}

void tick() {
  if (!ready) return; // No retries/restart loop after a known driver failure.
  const uint32_t started = micros();
#if DOG_RGB_BRINGUP_STAGE == 3
  commands();
#endif
  if (!enabled) {
    const uint32_t elapsed = micros() - started;
    if (elapsed > max_service_us) max_service_us = elapsed;
    return;
  }
  const uint32_t now = millis();
  bool drew = false;
  if (redraw) {
    redraw = false;
    if (test_pattern) {
      bars();
      dirty = 0;
    } else {
      frame();
      const DisplaySnapshot sample = capture_snapshot();
      sample_ms = last_sample_ms = sample.captured_ms;
      pending = format_view(sample);
      dirty = (1U << kRowCount) - 1U;
    }
    light(true);
    drew = true;
  } else if (!test_pattern) {
    if (dirty == 0 && time_utils::elapsed_at_least(now, last_sample_ms, kSampleMs)) {
      const DisplaySnapshot sample = capture_snapshot();
      sample_ms = last_sample_ms = sample.captured_ms;
      pending = format_view(sample);
      for (uint8_t i = 0; i < kRowCount; ++i) {
        if (strcmp(shown.rows[i], pending.rows[i]) != 0 ||
            (i == 0 && shown.gps_color != pending.gps_color)) dirty |= 1U << i;
      }
    }
    // At most one changed row per loop; GPS is serviced between row transfers.
    for (uint8_t i = 0; i < kRowCount; ++i) {
      if (dirty & (1U << i)) {
        row(i);
        dirty &= ~(1U << i);
        drew = true;
        break;
      }
    }
  }
  const uint32_t elapsed = micros() - started;
  if (elapsed > max_service_us) max_service_us = elapsed;
  if (drew) record_draw(elapsed);
}

void report(Print &sink) {
  char line[320];
  const int n = snprintf(line, sizeof(line),
      "[LCD] ready=%d enabled=%d light=%d test=%d sample_ms=%lu pending=%u "
      "init_us=%lu draw_ticks=%lu rows=%lu draw_max_us=%lu p95_upper_us=%lu tick_max_us=%lu\n",
      ready, enabled, backlight, test_pattern, static_cast<unsigned long>(sample_ms), dirty,
      static_cast<unsigned long>(init_us), static_cast<unsigned long>(draw_ticks),
      static_cast<unsigned long>(rows_drawn), static_cast<unsigned long>(max_tick_us),
      static_cast<unsigned long>(p95_upper_us()), static_cast<unsigned long>(max_service_us));
  if (n > 0 && static_cast<size_t>(n) < sizeof(line))
    sink.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(n));
}
} // namespace display
