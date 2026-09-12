"""Native I2 reception semantics, diagnostic presentation and real bus limits."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_led_bringup import NEOPIXEL_STUB

ROOT = Path(__file__).resolve().parents[1]

ARDUINO = r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <cstring>
#include <cassert>
using String = std::string;
inline uint32_t now_ms = 10000;
inline uint32_t millis() { return now_ms; }
class Print {
public:
  std::string text;
  size_t write(const uint8_t *data, size_t size) {
    text.append(reinterpret_cast<const char *>(data), size); return size;
  }
  void print(const char *value) {
    write(reinterpret_cast<const uint8_t *>(value), std::strlen(value));
  }
};
"""

HARNESS = r"""
#include <Arduino.h>
#include <limits>
#include "bringup/gps_check.h"
#include "bringup/bench_limits.h"
#include "gps/gps.h"
#include "config/runtime_config.h"
#include "led/led_ui.h"
#include "led/led_bus.h"

namespace {
gps::ReceptionInput input{10000, false, 10000, false, 10000, false, false};
bool usable = false;
float speed = 0;
RuntimeConfig config_value{};
led::PowerDiagnostics diagnostics{};
}
namespace gps {
ReceptionState reception_state() { input.now_ms = now_ms; return classify_reception(input); }
bool raw_fix() { return input.raw_fix; }
bool trusted_fix() { return input.trusted_fix; }
bool speed_usable() { return usable; }
float last_speed_kph() { return speed; }
float total_distance_m() { return 123.4f; }
uint32_t current_date() { return 20260912; }
uint8_t sats() { return 8; }
uint8_t fix_quality() { return 1; }
float hdop() { return 0.9f; }
bool has_byte_observation() { return input.byte_observed; }
bool has_rmc_observation() { return input.rmc_observed; }
unsigned long last_byte_ms() { return input.last_byte_ms; }
unsigned long last_rmc_ms() { return input.last_rmc_ms; }
unsigned long bytes_rx() { return 1234; }
unsigned long rmc_seen() { return 20; }
unsigned long gga_seen() { return 21; }
unsigned long stale_count() { return 1; }
unsigned long overflow() { return 0; }
unsigned long checksum_fail() { return 2; }
unsigned long parse_fail() { return 3; }
}
namespace config { const RuntimeConfig &get() { return config_value; } }
namespace led_ui { const led::PowerDiagnostics &power_diagnostics() { return diagnostics; } }

std::string report() {
  Print sink;
  bringup::gps_check::report(sink);
  assert(sink.text.size() < 512);
  assert(sink.text.find("day_m=123.4 date=20260912") != std::string::npos);
  assert(sink.text.find("rx=1234 rmc=20 gga=21") != std::string::npos);
  return sink.text;
}
void has(const std::string &value, const char *text) {
  assert(value.find(text) != std::string::npos);
}
int main() {
  using gps::ReceptionState;
  config_value.brightness = 255;
  config_value.led_power_limit_enabled = false;
  config_value.led_power_budget_ma = 5000;
  config_value.mode = MODE_SPEED;
  has(report(), "state=no-data");
  has(report(), "speed_kph=--");
  input.byte_observed = true;
  has(report(), "state=bytes-no-rmc"); // Bytes alone are not valid NMEA/fix.
  input.rmc_observed = true;
  has(report(), "state=searching");
  input.raw_fix = true;
  has(report(), "state=untrusted"); // Raw position may fail GGA quality gates.
  input.trusted_fix = true; usable = true; speed = 0;
  has(report(), "state=fix");
  has(report(), "speed_kph=0.00"); // Valid stationary sample is distinct from invalid.
  speed = 12.5f;
  has(report(), "speed_kph=12.50");
  usable = false;
  has(report(), "speed_kph=--");
  usable = true; speed = std::numeric_limits<float>::quiet_NaN();
  has(report(), "speed_kph=--");
  speed = 12.5f;

  now_ms = 13000;
  assert(gps::reception_state() == ReceptionState::Fix); // Exactly 3000 remains valid.
  now_ms = 13001;
  has(report(), "state=stale");
  has(report(), "speed_kph=--"); // Even if underlying booleans have not been expired yet.
  input.last_byte_ms = now_ms; // Other UART data cannot rejuvenate an old RMC.
  assert(gps::reception_state() == ReceptionState::Stale);
  input.last_rmc_ms = now_ms;
  has(report(), "state=fix");
  has(report(), "speed_kph=12.50");
  input.trusted_fix = false; input.raw_fix = false;
  has(report(), "state=searching"); // Live invalid RMC differs from lost reception.

  assert(!gps::uart_stale(5000, true, 0));
  assert(gps::uart_stale(5001, true, 0));
  assert(!gps::uart_stale(1000000, false, 0));
  assert(!gps::rmc_stale(1000000, false, 0));
  const uint32_t stamp = UINT32_MAX - 1000U;
  assert(!gps::rmc_stale(stamp + 3000U, true, stamp));
  assert(gps::rmc_stale(stamp + 3001U, true, stamp));
  assert(!gps::uart_stale(stamp + 5000U, true, stamp));
  assert(gps::uart_stale(stamp + 5001U, true, stamp));
  input = {stamp + 3001U, true, stamp + 3001U, true, stamp, true, true};
  assert(gps::classify_reception(input) == ReceptionState::Stale);
  input.last_rmc_ms = input.now_ms;
  assert(gps::classify_reception(input) == ReceptionState::Fix);

  // Reporting exposes effective/requested values without changing config.
  has(report(), "brightness=16/255 budget_ma=1000");
  assert(config_value.brightness == 255 && !config_value.led_power_limit_enabled);
  assert(config_value.led_power_budget_ma == 5000);

  // Exercise the real bus, not just the pure cap helper: covers begin and later
  // brightness/power changes made through normal portal apply paths.
  led::LedBus bus(LED_STRIP_COUNT, 17, 18, true);
  bus.configure_power({false, 5000, 990, 40, 40});
  bus.begin(255);
  for (const auto &strip : strips) assert(strip.brightness == 16);
  bus.set_brightness(8);
  for (const auto &strip : strips) assert(strip.brightness == 8);
  bus.set_brightness(255);
  for (const auto &strip : strips) assert(strip.brightness == 16);
  led::LedFrame frame{};
  for (auto &p : frame.bus_a) p = {255,255,255};
  for (auto &p : frame.bus_b) p = {255,255,255};
  bus.show(frame);
  assert(bus.power_diagnostics().requested_ma > 1000);
  assert(bus.power_diagnostics().estimated_ma <= 1000);
  assert(bus.power_diagnostics().scale < 255); // Requested disabled was overridden.
  bus.configure_power({false, 250, 200, 20, 20});
  bus.show(frame);
  assert(bus.power_diagnostics().estimated_ma <= 250); // Keep a lower budget.
  bus.set_brightness(0);
  for (const auto &strip : strips) assert(strip.brightness == 0);
  const auto effective = bringup::bench_limits::power({false, 5000, 321, 11, 12});
  assert(effective.enabled && effective.budget_ma == 1000);
  assert(effective.base_current_ma == 321 && effective.rgb_channel_ma == 11 &&
         effective.white_channel_ma == 12); // Calibration is preserved.
}
"""


class GpsBringupTests(unittest.TestCase):
    def test_reception_reports_and_transport_only_bench_limits(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "Arduino.h").write_text(ARDUINO, encoding="utf-8")
            (folder / "Adafruit_NeoPixel.h").write_text(
                NEOPIXEL_STUB.replace("brightness == 16", "brightness <= 16"), encoding="utf-8")
            (folder / "config.h").write_text(
                '#pragma GCC diagnostic push\n'
                '#pragma GCC diagnostic ignored "-Wunused-variable"\n'
                f'#include "{(ROOT / "include/config.h").as_posix()}"\n'
                '#pragma GCC diagnostic pop\n', encoding="utf-8")
            source = folder / "test.cpp"
            source.write_text(HARNESS, encoding="utf-8")
            executable = folder / "test.exe"
            sources = ["bringup/gps_check.cpp", "led/led_bus.cpp", "led/led_color.cpp",
                       "led/power_limiter.cpp"]
            result = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-DDOG_RGB_BOARD_WAVESHARE_LCD169_V2=1", "-DDOG_RGB_BRINGUP_STAGE=2",
                 "-I", str(folder), "-I", str(ROOT / "include"), str(source),
                 *[str(ROOT / "src" / path) for path in sources], "-o", str(executable)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stage_two_reuses_normal_boot_loop_and_queued_reporting(self):
        # Source contracts complement the native unit test; the actual target
        # build/link check verifies that the normal application is linked.
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        gps = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        self.assertEqual(main.count("defined(DOG_RGB_BRINGUP_STAGE) && DOG_RGB_BRINGUP_STAGE < 2"), 2)
        self.assertIn("#if DOG_RGB_BRINGUP_STAGE != 2\n    led_ui::start_welcome();", main)
        self.assertIn("#define Serial periodic_log\n#if DOG_RGB_BRINGUP_STAGE == 2", main)
        self.assertIn("bringup::gps_check::report(Serial);", main)
        self.assertIn("gps::uart_stale(now_ms, gps_byte_observed, gps_last_byte_ms)", gps)
        self.assertIn("gps::rmc_stale(now_ms, gps_rmc_observed, gps_last_rmc_ms)", gps)
        loop = main[main.index("void loop() {"):]
        self.assertLess(loop.index("gps::tick();"), loop.index("led_ui::tick();"))


if __name__ == "__main__":
    unittest.main()
