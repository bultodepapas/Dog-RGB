"""Execute I3 adapter/presentation/scheduler with a recording LCD transport."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARDUINO = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
using String = std::string;
inline uint32_t now_ms = 10000, now_us = 10000000;
inline uint32_t millis() { return now_ms; }
inline uint32_t micros() { return now_us; }
constexpr int LOW = 0, HIGH = 1;
inline int light_level = 0;
inline unsigned row_count = 0;
inline void digitalWrite(int pin, int value) {
  assert(pin == 15);
  if (value) assert(row_count >= 5);
  light_level = value;
}
class Print {
 public:
  std::string output;
  size_t write(const uint8_t *data, size_t n) {
    output.append(reinterpret_cast<const char *>(data), n); return n;
  }
};
class Console : public Print {
 public:
  std::string input;
  int available() { return static_cast<int>(input.size()); }
  int read() { int c = input.front(); input.erase(0, 1); return c; }
};
inline Console Serial;
'''
GFX = r'''
#pragma once
#include <Arduino.h>
constexpr int GFX_NOT_DEFINED = -1;
inline bool begin_result = true;
inline unsigned begins = 0, screens = 0, fills = 0, prints = 0;
inline std::vector<std::string> texts;
class Arduino_ESP32SPI {
 public:
  Arduino_ESP32SPI(int dc, int cs, int clk, int mosi, int miso) {
    assert(dc == 4 && cs == 5 && clk == 6 && mosi == 7 && miso == -1);
  }
};
class Arduino_ST7789 {
  int x_ = 0, y_ = 0, size_ = 1;
 public:
  Arduino_ST7789(Arduino_ESP32SPI *, int rst, int rotation, bool ips,
                int w, int h, int x1, int y1, int x2, int y2) {
    assert(rst == 8 && rotation == 0 && ips && w == 240 && h == 280);
    assert(x1 == 0 && y1 == 20 && x2 == 0 && y2 == 0);
  }
  bool begin(int hz) { assert(hz == 40000000); ++begins; now_us += 300000; return begin_result; }
  void fillScreen(uint16_t) { ++screens; now_us += 30000; }
  void setTextWrap(bool value) { assert(!value); }
  void setTextSize(int size) { size_ = size; }
  void setTextColor(uint16_t, uint16_t) {}
  void setCursor(int x, int y) { x_ = x; y_ = y; }
  void print(const char *s) {
    assert(x_ >= 24 && y_ >= 20 && y_ + 8 * size_ <= 260);
    assert(x_ + static_cast<int>(strlen(s)) * 6 * size_ <= 216);
    texts.push_back(s); ++prints; now_us += 400;
  }
  void drawFastHLine(int x, int y, int w, uint16_t) { assert(x+w<=240 && y<280); }
  void drawRect(int x, int y, int w, int h, uint16_t) {
    assert(x == 0 && y == 0 && w == 240 && h == 280);
  }
  void fillRect(int x, int y, int w, int h, uint16_t) {
    assert(x >= 0 && y >= 0 && x+w<=240 && y+h<=280);
    ++fills; now_us += 2000;
    if (w == 192) ++row_count;
  }
};
'''
HARNESS = r'''
#include <Arduino_GFX_Library.h>
#include <limits>
#include "display/display.h"
#include "display/text_view.h"
#include "bringup/display_demo.h"
#include "gps/gps.h"
#include "led/led_ui.h"
gps::ReceptionState state = gps::ReceptionState::NoData;
float speed = 0, distance_m = 123.4f;
bool usable = false;
uint32_t date = 20260912;
led::LedState led_state;
namespace gps {
ReceptionState reception_state() { return state; }
float last_speed_kph() { return speed; }
bool speed_usable() { return usable; }
float total_distance_m() { return distance_m; }
uint32_t current_date() { return date; }
}
namespace led_ui { const led::LedState &current_state() { return led_state; } }
std::string report() { Print p; display::report(p); return p.output; }
void has(const std::string &s, const char *value) { assert(s.find(value) != std::string::npos); }
void command(const char *s) { Serial.input = s; display::tick(); }
void next_sample() { now_ms += 1000; display::tick(); }
int main(int argc, char **) {
  if (argc > 1) {
    begin_result = false;
    assert(!display::begin()); assert(!display::begin());
    for (int i = 0; i < 100; ++i) display::tick();
    assert(begins == 1 && screens == 0 && light_level == LOW);
    has(report(), "ready=0"); return 0;
  }
  auto sample = display::capture_snapshot();
  assert(sample.captured_ms == now_ms && !sample.speed_valid);
  assert(sample.daily_distance_m == 123.4f && sample.distance_date == date);
  has(display::format_view(sample).rows[1], "--");
  assert(display::begin()); assert(display::begin());
  assert(begins == 1 && screens == 1 && row_count == 5 && light_level == HIGH);
  unsigned before = prints;
  for (int i = 0; i < 50; ++i) display::tick();
  next_sample(); assert(prints == before); // No redraw of equal strings.
  state = gps::ReceptionState::Fix; usable = true;
  now_ms += 999; display::tick(); assert(prints == before);
  now_ms += 1; display::tick(); assert(prints == before + 1);
  display::tick(); assert(prints == before + 2); // Next row on another loop.
  has(texts.back(), "0.0");
  sample = display::capture_snapshot(); assert(sample.speed_valid);
  has(display::format_view(sample).rows[0], "FIX VALIDO");
  speed = 12.34f; sample = display::capture_snapshot();
  has(display::format_view(sample).rows[1], "12.3");
  for (auto s : {gps::ReceptionState::NoData, gps::ReceptionState::Receiving,
                gps::ReceptionState::Searching, gps::ReceptionState::Untrusted,
                gps::ReceptionState::Stale}) {
    state = s; sample = display::capture_snapshot(); assert(!sample.speed_valid);
    auto view = display::format_view(sample);
    has(view.rows[1], "--"); has(view.rows[2], "123 m"); has(view.rows[3], "2026-09-12");
  }
  state = gps::ReceptionState::Fix;
  for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
    speed = invalid; sample = display::capture_snapshot();
    assert(!sample.speed_valid); has(display::format_view(sample).rows[1], "--");
  }
  speed = 10000; distance_m = 1e20f; date = 0;
  sample = display::capture_snapshot(); auto view = display::format_view(sample);
  has(view.rows[1], "999+"); has(view.rows[2], ">999 km"); has(view.rows[3], "Fecha sin registrar");
  for (auto mode : {led::LedMode::Speed, led::LedMode::Geofence, led::LedMode::Show, led::LedMode::Simple}) {
    led_state.mode = mode; sample = display::capture_snapshot();
    assert(sample.led_mode == mode);
    has(display::format_view(sample).rows[4], led::led_mode_name(mode));
  }
  next_sample(); for (int i=0;i<5;++i) display::tick();
  assert(screens == 1); // All normal updates remain partial.
  command("b"); assert(light_level == LOW);
  next_sample(); assert(light_level == LOW); // Sampling doesn't override backlight.
  command("b"); assert(light_level == HIGH);
  command("d"); assert(light_level == LOW); before = prints;
  next_sample(); assert(prints == before); has(report(), "enabled=0");
  command("d"); for (int i=0;i<5;++i) display::tick();
  assert(light_level == HIGH); has(report(), "enabled=1");
  command("t"); has(texts.back(), "240 x 280");
  before = prints; next_sample(); assert(prints == before);
  command("v"); for (int i=0;i<5;++i) display::tick();
  has(report(), "test=0");
  command("xxxxxxxxx"); assert(Serial.input == "x"); display::tick();
  command("r"); has(report(), "draw_ticks=0"); has(report(), "p95_upper_us=0");
  // Explicit LCD-only demo preserves the real getters/configured mode.
  const auto real_sample = display::capture_snapshot();
  command("f"); for (int i=0;i<5;++i) display::tick();
  has(report(), "demo=1");
  assert(display::capture_snapshot().gps_state == real_sample.gps_state);
  assert(display::capture_snapshot().daily_distance_m == real_sample.daily_distance_m);
  const gps::ReceptionState expected[] = {gps::ReceptionState::Searching,
      gps::ReceptionState::Fix, gps::ReceptionState::Fix,
      gps::ReceptionState::Untrusted, gps::ReceptionState::Stale, gps::ReceptionState::Fix};
  for (unsigned i=0;i<6;++i) {
    const auto fixture = bringup::display_demo(real_sample, i*5000);
    assert(fixture.gps_state == expected[i] && fixture.led_mode == real_sample.led_mode);
    assert(fixture.captured_ms == real_sample.captured_ms);
    assert(fixture.daily_distance_m == 1842 && fixture.distance_date == 20260912);
  }
  command("v"); for (int i=0;i<5;++i) display::tick(); has(report(), "demo=0");
  command("r");
  // Resample across millis rollover, preserving the 1 Hz rule.
  now_ms = UINT32_MAX - 500; display::tick(); for (int i=0;i<5;++i) display::tick();
  speed = 6; before = prints;
  now_ms += 999; display::tick(); assert(prints == before);
  now_ms += 1; display::tick(); assert(prints == before + 1);
  has(texts.back(), "6.0"); has(report(), "p95_upper_us=5000");
}
'''


class DisplayTests(unittest.TestCase):
    def test_adapter_rendering_cadence_controls_and_detected_failure(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "Arduino.h").write_text(ARDUINO, encoding="utf-8")
            (folder / "Arduino_GFX_Library.h").write_text(GFX, encoding="utf-8")
            (folder / "config.h").write_text(
                '#pragma GCC diagnostic push\n'
                '#pragma GCC diagnostic ignored "-Wunused-variable"\n'
                f'#include "{(ROOT / "include/config.h").as_posix()}"\n'
                '#pragma GCC diagnostic pop\n', encoding="utf-8")
            (folder / "test.cpp").write_text(HARNESS, encoding="utf-8")
            executable = folder / "test.exe"
            result = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-DDOG_RGB_BOARD_WAVESHARE_LCD169_V2=1", "-DDOG_RGB_DISPLAY_ENABLED=1",
                 "-DDOG_RGB_BRINGUP_STAGE=3", "-I", str(folder), "-I", str(ROOT / "include"),
                 str(folder / "test.cpp"), *[str(ROOT / "src" / p) for p in
                 ("display/display.cpp", "display/snapshot.cpp", "display/text_view.cpp", "led/led_state.cpp")],
                 "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for args in ([], ["fail"]):
                result = subprocess.run([str(executable), *args], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_display_hooks_follow_normal_work_and_use_queued_reports(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        setup = main[main.index("void setup() {"):main.index("void loop() {")]
        loop = main[main.index("void loop() {"):]
        self.assertLess(setup.index("portal_http::begin();"), setup.index("display::begin();"))
        self.assertLess(loop.rindex("portal_http::handle_client();"), loop.index("display::tick();"))
        self.assertLess(loop.index("display::tick();"), loop.index("const unsigned long loop_elapsed_us"))
        logs = main[main.index("#define Serial periodic_log"):main.index('#pragma pop_macro("Serial")')]
        self.assertIn("display::report(Serial);", logs)
        for path in ("include/display/snapshot.h", "include/display/text_view.h", "src/display/text_view.cpp"):
            self.assertNotIn("Arduino.h", (ROOT / path).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
