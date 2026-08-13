from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class DayModeStaticTests(unittest.TestCase):
    def test_day_mode_contract_is_declared(self):
        config_h = read("include/config.h")
        gps_h = read("include/gps/gps.h")
        day_h = read("include/power/day_mode.h")

        self.assertIn("DAY_MODE_START_MIN", config_h)
        self.assertIn("DAY_MODE_END_MIN", config_h)
        self.assertIn("DAY_MODE_TZ_OFFSET_MIN", config_h)
        self.assertIn("DAY_MODE_TIME_STALE_MS", config_h)

        self.assertIn("bool has_time();", gps_h)
        self.assertIn("uint16_t local_time_min(int16_t offset_min);", gps_h)
        self.assertIn("unsigned long last_time_ms();", gps_h)

        self.assertIn("bool enabled();", day_h)
        self.assertIn("bool time_available();", day_h)
        self.assertIn("uint16_t local_min();", day_h)
        self.assertIn("bool active_now();", day_h)
        self.assertIn("const char *state_name();", day_h)


    def test_day_mode_logic_uses_gps_time_and_window(self):
        day_cpp = read("src/power/day_mode.cpp")

        self.assertIn("config::get().day_mode_enabled", day_cpp)
        self.assertIn("gps::has_time()", day_cpp)
        self.assertIn("DAY_MODE_START_MIN", day_cpp)
        self.assertIn("DAY_MODE_END_MIN", day_cpp)
        self.assertIn("DAY_MODE_TZ_OFFSET_MIN", day_cpp)
        self.assertIn("waiting_time", day_cpp)
        self.assertIn("outside_window", day_cpp)


    def test_led_ui_keeps_status_leds_during_day_mode(self):
        led_cpp = read("src/led/led_ui.cpp")
        policy_cpp = read("src/led/led_policy.cpp")

        self.assertIn('#include "power/day_mode.h"', led_cpp)
        self.assertIn("clear_body_leds()", led_cpp)
        self.assertIn("day_mode::active_now()", led_cpp)
        self.assertIn("paint_status_leds(now_ms, gps_ok, sta_ok, sta_try, critical_error)", led_cpp)
        self.assertIn("if (!active_led_state.body_enabled)", led_cpp)
        self.assertIn("if (input.day_mode_active)", policy_cpp)
        self.assertIn("state.intent = LedIntent::DayStatus", policy_cpp)
        self.assertIn("state.body_enabled = false", policy_cpp)

    def test_welcome_runs_before_day_mode_gate(self):
        led_cpp = read("src/led/led_ui.cpp")

        update_idx = led_cpp.index("static void update_led_ui()")
        welcome_idx = led_cpp.index("if (welcome.active)", update_idx)
        policy_idx = led_cpp.index("active_led_state = evaluate_policy", welcome_idx)
        self.assertLess(welcome_idx, policy_idx)
        self.assertIn(
            "update_welcome(now_ms);\n    return;",
            led_cpp[welcome_idx:policy_idx],
        )


if __name__ == "__main__":
    unittest.main()
