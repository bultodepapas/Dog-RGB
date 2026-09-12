"""Exercise I1 orchestration through the real LedBus and PowerLimiter."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_board_profiles import ARDUINO_STUB

ROOT = Path(__file__).resolve().parents[1]

NEOPIXEL_STUB = r"""
#pragma once
#include <stdint.h>
#include <cassert>
#include <vector>
constexpr int NEO_GRBW = 0x123, NEO_KHZ800 = 0;
struct Pixel { uint8_t r=0, g=0, b=0, w=0; };
struct StripState {
  int pin;
  uint8_t brightness = 255;
  unsigned shows = 0;
  std::vector<Pixel> pixels;
};
inline std::vector<StripState> strips;
class Adafruit_NeoPixel {
  unsigned slot;
public:
  Adafruit_NeoPixel(uint16_t count, int16_t pin, int type) {
    assert(type == NEO_GRBW + NEO_KHZ800);
    slot = strips.size();
    strips.push_back({pin, 255, 0, std::vector<Pixel>(count)});
  }
  void begin() {}
  void setBrightness(uint8_t brightness) {
    assert(brightness <= 16);
    strips[slot].brightness = brightness;
  }
  void clear() { for (auto &p : strips[slot].pixels) p = {}; }
  void setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    assert(i < strips[slot].pixels.size());
    strips[slot].pixels[i] = {r,g,b,w};
  }
  void show() {
    assert(strips[slot].brightness == 16);
    ++strips[slot].shows;
  }
};
"""

HARNESS = r"""
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <string>
#include "bringup/bringup.h"
#include "bringup/led_check.h"
#include "led/led_bus.h"
using namespace bringup::led_check;

bool black(const Pixel &p) { return !p.r && !p.g && !p.b && !p.w; }
void assert_off() {
  for (const auto &s : strips) for (const auto &p : s.pixels) assert(black(p));
}
void command(char c, uint32_t now) { Serial.input.push_back(c); tick(now); }
void ready(uint32_t now) { Serial.connected = true; tick(now); }
void complete_one(uint32_t now) {
  command('1', now);
  for (unsigned step = 0; step < kSteps; ++step) tick(now + step * kStepMs);
  tick(now + kOnePixelMs);
  assert_off();
}
int main() {
  // Exercise the common entry as well as the LED controller.
  bringup::begin();
  assert(strips.size() == 2 && strips[0].pin == 17 && strips[1].pin == 18);
  for (const auto &s : strips) {
    assert(s.pixels.size() == LED_STRIP_COUNT && s.brightness == 16 && s.shows >= 1);
  }
  assert_off(); // Boot clears both full-length strips before any command.
  ready(0);
  command('f', 1); // Full strip cannot start before the one-pixel exercise.
  assert_off();

  command('1', 100);
  for (unsigned step = 0; step < kSteps; ++step) {
    tick(100 + step * kStepMs);
    const unsigned group = step / 5, color = step % 5;
    for (unsigned bus = 0; bus < 2; ++bus) {
      for (unsigned i = 0; i < strips[bus].pixels.size(); ++i) {
        const Pixel &p = strips[bus].pixels[i];
        bool lit = i == 0 && (group == 2 || group == bus) && color != 4;
        if (!lit) assert(black(p));
        else {
          assert(p.r == (color == 0 ? 255 : 0));
          assert(p.g == (color == 1 ? 255 : 0));
          assert(p.b == (color == 2 ? 255 : 0));
          assert(p.w == (color == 3 ? 255 : 0)); // White uses W, not RGB mixing.
        }
      }
    }
    const unsigned shows = strips[0].shows;
    for (unsigned i = 0; i < 50; ++i) tick(100 + step * kStepMs);
    assert(strips[0].shows == shows); // No repeat transport at unchanged time.
  }
  tick(100 + kOnePixelMs);
  assert_off();

  // Full test covers every configured pixel; both buses share the same limiter.
  const uint32_t full_start = 40000;
  command('f', full_start);
  tick(full_start + 10 * kStepMs); // Both red.
  for (const auto &s : strips) for (const auto &p : s.pixels) assert(p.r == 255);
  tick(full_start + kFullTestMs);
  assert_off(); // Automatic fifteen-minute deadline, not an endless pattern.

  command('f', 1000000);
  assert(!black(strips[0].pixels[0]));
  Serial.input = "f01";
  tick(1000001);
  assert_off(); // Stop wins within one command batch.
  Serial.input = std::string(40, 'f');
  tick(1000002);
  assert(Serial.input.size() == 24); // Bounded draining, no busy wait.
  tick(1000003); tick(1000004);
  assert_off();

  command('f', 1100000);
  Serial.connected = false;
  tick(1100001);
  assert_off(); // Disconnect sends black instead of retaining the last frame.
  Serial.input = "f";
  ready(1100002);
  assert_off(); // Reconnection discards stale commands and does not restart.

  // A first test with skipped steps is not treated as completed.
  command('1', 1200000);
  tick(1200000 + kOnePixelMs);
  command('f', 1240000);
  assert_off();

  // Rollover during a real complete first cycle preserves ordering/deadlines.
  const uint32_t near_wrap = UINT32_MAX - 12000U;
  complete_one(near_wrap);
  command('f', near_wrap + kOnePixelMs + 1000U);
  assert(!black(strips[0].pixels[0]));
  tick(near_wrap + kOnePixelMs + 1000U + kFullTestMs);
  assert_off();

  // Check the actual bus + limiter response under an intentionally tight
  // synthetic budget, without altering the board's persisted configuration.
  led::LedBus limited(LED_STRIP_COUNT, 17, 18, true);
  limited.configure_power({true, 201, 200, 20, 20});
  limited.begin(kBrightness);
  led::LedFrame frame{};
  for (auto &p : frame.bus_a) p = {255,255,255};
  for (auto &p : frame.bus_b) p = {255,255,255};
  limited.show(frame);
  const auto &power = limited.power_diagnostics();
  assert(power.requested_ma > 201 && power.estimated_ma <= 201);
  assert(power.scale < 255 && power.frames_limited == 1);
  for (unsigned bus = 2; bus < 4; ++bus) {
    for (const auto &p : strips[bus].pixels) {
      assert(p.r == 0 && p.g == 0 && p.b == 0 && p.w < 255);
    }
  }
}
"""


class LedBringupTests(unittest.TestCase):
    def test_commands_frames_rgbw_limiter_timeouts_and_disconnect(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            arduino = ARDUINO_STUB.replace(
                "#include <vector>", "#include <vector>\n#include <string>")
            arduino = arduino.replace("unsigned reports = 0;", """
  unsigned reports = 0;
  std::string input;
  int available() const { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    const char c = input.front(); input.erase(0, 1); return c;
  }
""")
            (folder / "Arduino.h").write_text(arduino, encoding="utf-8")
            # config.h contains existing AP string pointers unused by this
            # native LED-only harness. Suppress that header's warning only;
            # retain -Werror for the diagnostic, bus and limiter sources.
            (folder / "config.h").write_text(
                '#pragma GCC diagnostic push\n'
                '#pragma GCC diagnostic ignored "-Wunused-variable"\n'
                f'#include "{(ROOT / "include/config.h").as_posix()}"\n'
                '#pragma GCC diagnostic pop\n', encoding="utf-8")
            (folder / "Adafruit_NeoPixel.h").write_text(NEOPIXEL_STUB, encoding="utf-8")
            (folder / "esp_system.h").write_text(
                "#pragma once\ninline int esp_reset_reason() { return 1; }\n", encoding="utf-8")
            source = folder / "test.cpp"
            source.write_text(HARNESS, encoding="utf-8")
            executable = folder / "test.exe"
            sources = ["bringup/bringup.cpp", "bringup/led_check.cpp", "led/led_bus.cpp",
                       "led/led_color.cpp", "led/power_limiter.cpp"]
            result = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-DDOG_RGB_BOARD_WAVESHARE_LCD169_V2=1", "-DDOG_RGB_BRINGUP_STAGE=1",
                 "-I", str(folder), "-I", str(ROOT / "include"), str(source),
                 *[str(ROOT / "src" / path) for path in sources], "-o", str(executable)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
