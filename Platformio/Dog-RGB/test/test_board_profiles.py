"""Execute board GPIO behavior and reject incompatible build selections."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLASSIC = "DOG_RGB_BOARD_XIAO_S3=1"
DISPLAY = "DOG_RGB_BOARD_WAVESHARE_LCD169_V2=1"

ARDUINO_STUB = r"""
#pragma once
#include <stdint.h>
#include <cassert>
#include <vector>
constexpr int INPUT = 0, OUTPUT = 1, LOW = 0, HIGH = 1;
struct IoEvent { bool mode; int pin; int value; };
inline std::vector<IoEvent> events;
inline uint32_t clock_ms = 0;
inline void pinMode(int pin, int value) {
  assert(pin >= 0 && pin <= 48);
  events.push_back({true, pin, value});
}
inline void digitalWrite(int pin, int value) {
  assert(pin >= 0 && pin <= 48);
  events.push_back({false, pin, value});
}
inline int digitalRead(int pin) { assert(pin == 40); return HIGH; }
inline uint32_t millis() { return clock_ms; }
struct SerialStub {
  bool connected = false;
  unsigned reports = 0;
  explicit operator bool() const { return connected; }
  template<typename... Args> void printf(const char *, Args...) { ++reports; }
};
inline SerialStub Serial;
struct EspStub {
  uint32_t getFlashChipSize() { return 16777216; }
  uint32_t getPsramSize() { return 8388608; }
  uint32_t getFreeHeap() { return 250000; }
  uint32_t getMinFreeHeap() { return 240000; }
};
inline EspStub ESP;
"""

HARNESS = r"""
#include <Arduino.h>
#include "pins.h"
#include "board/board_io.h"
#include "bringup/bringup.h"

static_assert(PIN_GPS_RX == 44 && PIN_GPS_TX == 43);
static_assert(!board::is_external_pin_available(-1));
static_assert(!board::is_external_pin_available(19));
static_assert(!board::is_external_pin_available(20));
#if defined(DOG_RGB_BOARD_XIAO_S3)
static_assert(PIN_LED_A_DATA == 1 && PIN_LED_B_DATA == 2 && PIN_STATUS_LED == 3);
static_assert(!board::kHasDisplay && !board::kHasTouch);
#else
static_assert(PIN_LED_A_DATA == 17 && PIN_LED_B_DATA == 18 && PIN_STATUS_LED == -1);
static_assert(board::kHasDisplay && !board::kHasTouch);
static_assert(!board::is_external_pin_available(1));
static_assert(!board::is_external_pin_available(41));
static_assert(!board::is_external_pin_available(35));
#endif
#if defined(DOG_RGB_WOKWI_SIM)
static_assert(PIN_WOKWI_SERIAL_RX == 8 && PIN_WOKWI_SERIAL_TX == 9);
#endif
int main() {
  board::begin();
#if defined(DOG_RGB_BOARD_XIAO_S3)
  assert(events.size() == 2);
  assert(events[0].mode && events[0].pin == 3 && events[0].value == OUTPUT);
  assert(!events[1].mode && events[1].pin == 3 && events[1].value == LOW);
  board::write_status(true);
  board::write_status(false);
  assert(events.size() == 4 && events[2].value == HIGH && events[3].value == LOW);
#else
  assert(events.size() == 5);
  assert(events[0].mode && events[0].pin == 41 && events[0].value == OUTPUT);
  assert(!events[1].mode && events[1].pin == 41 && events[1].value == HIGH);
  assert(events[2].mode && events[2].pin == 40 && events[2].value == INPUT);
  assert(events[3].mode && events[3].pin == 15 && events[3].value == OUTPUT);
  assert(!events[4].mode && events[4].pin == 15 && events[4].value == LOW);
  board::write_status(true);
  board::write_status(false);
  assert(events.size() == 5); // No absent status LED or external strip writes.
#endif
#if defined(DOG_RGB_BRINGUP_STAGE)
  clock_ms = UINT32_MAX - 500U;
  bringup::begin(); // A disconnected USB monitor must not block startup.
  assert(Serial.reports == 0);
  Serial.connected = true;
  bringup::tick(clock_ms + 999U);
  assert(Serial.reports == 0);
  bringup::tick(clock_ms + 1000U); // Crosses millis() wrap.
  assert(Serial.reports == 1);
  for (int i = 0; i < 100; ++i) bringup::tick(clock_ms + 1000U);
  assert(Serial.reports == 1);
  bringup::tick(clock_ms + 2000U);
  assert(Serial.reports == 2);
  assert(events.size() == 5); // Reporting never changes outputs.
#endif
}
"""


class BoardProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++")
        if not cls.compiler:
            raise AssertionError("g++ is required for board profile tests")

    def test_gpio_behavior_for_all_four_targets(self):
        selections = [
            [CLASSIC], [CLASSIC, "DOG_RGB_WOKWI_SIM=1"],
            [DISPLAY], [DISPLAY, "DOG_RGB_BRINGUP_STAGE=0"],
        ]
        for flags in selections:
            with self.subTest(flags=flags), tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                (folder / "Arduino.h").write_text(ARDUINO_STUB, encoding="utf-8")
                (folder / "esp_system.h").write_text(
                    "#pragma once\ninline int esp_reset_reason() { return 1; }\n",
                    encoding="utf-8")
                source = folder / "board_test.cpp"
                source.write_text(HARNESS, encoding="utf-8")
                executable = folder / "board_test.exe"
                command = [self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                           "-pedantic", "-I", str(folder), "-I", str(ROOT / "include"),
                           *["-D" + flag for flag in flags], str(source),
                           str(ROOT / "src/board/board_io.cpp")]
                if "DOG_RGB_BRINGUP_STAGE=0" in flags:
                    command.append(str(ROOT / "src/bringup/bringup.cpp"))
                result = subprocess.run(command + ["-o", str(executable)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_conflicting_and_unimplemented_selections_fail(self):
        invalid = [[], [CLASSIC, DISPLAY], ["DOG_RGB_BOARD_XIAO_S3=0"],
                   ["DOG_RGB_BOARD_UNKNOWN=1"], [DISPLAY, "DOG_RGB_WOKWI_SIM=1"],
                   [CLASSIC, "DOG_RGB_BRINGUP_STAGE=0"],
                   [DISPLAY, "DOG_RGB_BRINGUP_STAGE=4"],
                   [DISPLAY, "DOG_RGB_BRINGUP_STAGE=3"],
                   [CLASSIC, "DOG_RGB_DISPLAY_ENABLED=1"],
                   [DISPLAY, "DOG_RGB_DISPLAY_ENABLED=0"],
                   [DISPLAY, "DOG_RGB_DISPLAY_ENABLED=1", "DOG_RGB_BRINGUP_STAGE=2"],
                   [DISPLAY, "DOG_RGB_BRINGUP_STAGE=-1"]]
        for flags in invalid:
            with self.subTest(flags=flags):
                result = subprocess.run(
                    [self.compiler, "-std=c++17", "-fsyntax-only", "-x", "c++",
                     "-I", str(ROOT / "include"), *["-D" + flag for flag in flags], "-"],
                    input='#include "pins.h"\n', capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("error:", result.stderr)

    def test_manifest_uses_generic_s3_with_explicit_memory_and_usb(self):
        manifest = json.loads((ROOT / "boards/waveshare_lcd169_v2.json").read_text())
        self.assertEqual(manifest["build"]["variant"], "esp32s3")
        self.assertEqual(manifest["build"]["arduino"]["memory_type"], "qio_opi")
        self.assertEqual(manifest["upload"]["flash_size"], "16MB")
        self.assertIn("-DBOARD_HAS_PSRAM", manifest["build"]["extra_flags"])
        self.assertIn("-DARDUINO_USB_CDC_ON_BOOT=1", manifest["build"]["extra_flags"])


if __name__ == "__main__":
    unittest.main()
