import argparse
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from display_bench import parse_step, summarize


class DisplayBenchTests(unittest.TestCase):
    def test_missing_records_never_become_a_success(self):
        result = summarize("")
        self.assertEqual(result["missing"], ["LCD", "SYS", "I3"])
        self.assertIsNone(result["observed_sys_span_s"])
        self.assertIsNone(result["lcd_tick_max_us"])
        self.assertNotIn("passed", result)

    def test_real_and_demo_data_remain_distinct(self):
        result = summarize("\n".join([
            "[LCD] ready=1 enabled=1 light=1 test=0 demo=1 tick_max_us=4500 p95_upper_us=5000 ui=lvgl page=activity",
            "[I3] ms=1000 state=no-data rx=0 overflow=0",
            "[SYS] uptime_s=30 heap=250000 min_heap=249000 log_drop_bytes=0",
            "[LCD] ready=1 enabled=0 light=0 test=0 demo=1 tick_max_us=4500 p95_upper_us=5000 ui=text page=text",
            "[I3] ms=32000 state=no-data rx=0 overflow=0",
            "[SYS] uptime_s=60 heap=250008 min_heap=248000 log_drop_bytes=2",
        ]))
        self.assertEqual(result["observed_sys_span_s"], 30)
        self.assertEqual(result["gps_states"], ["no-data"])
        self.assertEqual(result["gps_rx"]["last"], 0)
        self.assertEqual(result["lcd_draw_p95_upper_max_us"], 5000)
        self.assertEqual(result["log_drop_bytes"]["last"], 2)
        self.assertEqual(len(result["lcd_states"]), 2)
        self.assertEqual(result["missing"], [])
        self.assertEqual(result["lcd_backends"], ["lvgl", "text"])
        self.assertEqual(result["lcd_pages"], ["activity", "text"])

    def test_partial_records_and_counter_wrap_are_reportable(self):
        result = summarize("\n".join([
            "[LCD] ready=1", "[LCD] ready=1 enabled=1 light=1 test=0 demo=0",
            "[I3] ms=4294967000 state=no-data", "[I3] ms=10 state=no-data",
            "[I3] ms=1000 state=no-data", "[I3] ms=100 state=no-data",
            "[BOOT] reset_reason=SW", "Guru Meditation Error: core panic",
        ]))
        self.assertEqual(result["gps_clock_regressions"], 1)
        self.assertEqual(result["boot_lines"], 1)
        self.assertIn("guru meditation", result["fatal_markers"])
        self.assertIsNone(result["heap"])

    def test_only_bounded_lcd_commands_are_accepted(self):
        self.assertEqual(parse_step("5.5:f"), (5.5, "f"))
        for command in "tvbdfrslacn":
            self.assertEqual(parse_step(f"0:{command}"), (0.0, command))
        for value in ("-1:f", "nan:f", "inf:f", "0:erase", "0:ff", "x:f", "2:x", "f"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                parse_step(value)

    def test_joined_records_do_not_mix_counters_or_hide_resets(self):
        result = summarize("\n".join([
            "[I3] ms=13369 state=n[I3] ms=66169 state=no-data rx=999 overflow=9",
            "[LCD] ready=1 tick_max_us=999999[SYS] uptime_s=999 heap=1",
            "[I3] ms=70000 state=no-data rx=0 overflow=0",
            "[LCD] ready=1 tick_max_us=4500",
            "[SYS] uptime_s=70 heap=220000",
            "[LCD] ready=1[BOOT] reset_reason=SW Guru Meditation Error",
        ]))
        self.assertEqual(result["malformed_records"], 3)
        self.assertEqual(result["gps_reports"], 1)
        self.assertEqual(result["gps_states"], ["no-data"])
        self.assertEqual(result["gps_rx"]["max"], 0)
        self.assertEqual(result["lcd_tick_max_us"], 4500)
        self.assertEqual(result["heap"]["min"], 220000)
        self.assertEqual(result["boot_lines"], 1)
        self.assertIn("guru meditation", result["fatal_markers"])


if __name__ == "__main__":
    unittest.main()
