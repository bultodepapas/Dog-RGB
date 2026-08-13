import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = PROJECT_ROOT.parents[1]
MASK32 = 0xFFFFFFFF
HALF_RANGE = 0x80000000


def elapsed_ms(now_ms: int, since_ms: int) -> int:
    return (now_ms - since_ms) & MASK32


def deadline_reached(now_ms: int, deadline_ms: int) -> bool:
    return elapsed_ms(now_ms, deadline_ms) < HALF_RANGE


def age_ms(now_ms: int, observed_ms: int) -> int:
    future = elapsed_ms(observed_ms, now_ms)
    if 0 < future <= 1000:
        return 0
    return elapsed_ms(now_ms, observed_ms)


class TimeRolloverTests(unittest.TestCase):
    def test_elapsed_thresholds_cross_uint32_wrap(self):
        observed = MASK32 - 4
        self.assertEqual(elapsed_ms(5, observed), 10)
        self.assertLess(elapsed_ms(4, observed), 10)
        self.assertGreater(elapsed_ms(6, observed), 10)

    def test_deadlines_remain_ordered_across_wrap(self):
        deadline = 5
        self.assertFalse(deadline_reached(MASK32 - 10, deadline))
        self.assertTrue(deadline_reached(deadline, deadline))
        self.assertTrue(deadline_reached(6, deadline))

    def test_age_distinguishes_rollover_from_same_loop_skew(self):
        self.assertEqual(age_ms(5, MASK32 - 4), 10)
        self.assertEqual(age_ms(100, 103), 0)
        self.assertEqual(age_ms(100, 1101), (100 - 1101) & MASK32)

    def test_firmware_uses_explicit_observation_state_and_shared_helpers(self):
        header = (PROJECT_ROOT / "include/util/time_utils.h").read_text(encoding="utf-8")
        gps = (PROJECT_ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        main = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
        portal = (PROJECT_ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")
        pages = (REPO_ROOT / "webui/src/pages/dev.html").read_text(encoding="utf-8")
        wifi = (PROJECT_ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")

        self.assertIn("static_assert(time_utils::elapsed_ms", header)
        self.assertIn("time_utils::elapsed_more_than", gps)
        self.assertIn("time_utils::elapsed_at_most", gps)
        self.assertIn("gps_byte_observed", gps)
        self.assertIn("gps_time_observed", gps)
        self.assertIn("time_utils::age_ms", main)
        self.assertIn("time_utils::age_ms", portal)
        self.assertIn("time_utils::deadline_reached", wifi)
        self.assertIn("sta_retry_scheduled", wifi)
        self.assertIn("ap_hold_scheduled", wifi)
        self.assertIn('wifiDiag["sta_retry_remaining_ms"]', portal)
        self.assertIn('wifiDiag["ap_hold_remaining_ms"]', portal)
        self.assertNotIn("diag.ap_hold_until_ms > nowMs", pages)
        self.assertNotIn("diag.next_sta_retry_ms > nowMs", pages)
        self.assertNotIn("now_ms >= gps_last", gps)
        self.assertNotIn("now_ms >= gps::last", main)
        self.assertNotIn("now_ms >= gps::last", portal)


if __name__ == "__main__":
    unittest.main()
