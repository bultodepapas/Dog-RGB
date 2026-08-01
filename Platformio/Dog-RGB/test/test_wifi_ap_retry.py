from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
MASK32 = 0xFFFFFFFF
HALF_RANGE = 0x80000000


def deadline_reached(now_ms: int, deadline_ms: int) -> bool:
    return ((now_ms - deadline_ms) & MASK32) < HALF_RANGE


class ApRetryModel:
    def __init__(self, initial_ms=1000, maximum_ms=30000):
        self.initial_ms = initial_ms
        self.maximum_ms = maximum_ms
        self.backoff_ms = initial_ms
        self.deadline_ms = 0
        self.scheduled = False
        self.attempts = 0
        self.delays = []

    def ready(self, now_ms: int) -> bool:
        return not self.scheduled or deadline_reached(now_ms, self.deadline_ms)

    def fail(self, now_ms: int):
        self.attempts += 1
        delay_ms = self.backoff_ms
        self.delays.append(delay_ms)
        self.deadline_ms = (now_ms + delay_ms) & MASK32
        self.scheduled = True
        self.backoff_ms = min(delay_ms * 2, self.maximum_ms)

    def succeed(self):
        self.backoff_ms = self.initial_ms
        self.deadline_ms = 0
        self.scheduled = False


class WifiApRetryTests(unittest.TestCase):
    def test_twenty_failures_are_exponential_and_bounded(self):
        model = ApRetryModel()
        now_ms = 0
        for _ in range(20):
            self.assertTrue(model.ready(now_ms))
            model.fail(now_ms)
            self.assertFalse(model.ready((now_ms + model.delays[-1] - 1) & MASK32))
            now_ms = model.deadline_ms

        self.assertEqual(model.delays[:6], [1000, 2000, 4000, 8000, 16000, 30000])
        self.assertEqual(model.delays[6:], [30000] * 14)
        self.assertEqual(max(model.delays), 30000)

    def test_hot_loop_cannot_retry_before_deadline(self):
        model = ApRetryModel()
        model.fail(0)
        ready_ticks = [tick for tick in range(1000) if model.ready(tick)]
        self.assertEqual(ready_ticks, [])
        self.assertTrue(model.ready(1000))

    def test_recovery_resets_next_failure_to_initial_delay(self):
        model = ApRetryModel()
        model.fail(0)
        model.fail(model.deadline_ms)
        model.fail(model.deadline_ms)
        self.assertEqual(model.backoff_ms, 8000)
        model.succeed()
        model.fail(12345)
        self.assertEqual(model.delays[-1], 1000)

    def test_deadline_is_safe_across_millis_rollover(self):
        model = ApRetryModel()
        start = MASK32 - 499
        model.fail(start)
        self.assertEqual(model.deadline_ms, 500)
        self.assertFalse(model.ready(MASK32))
        self.assertFalse(model.ready(499))
        self.assertTrue(model.ready(500))

    def test_firmware_gates_runtime_attempts_and_checks_required_stages(self):
        config = (ROOT / "include/config.h").read_text(encoding="utf-8")
        wifi = (ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        enable = wifi.split("void enable_ap", 1)[1].split("void disable_ap", 1)[0]
        start = wifi.split("bool start_ap_radio", 1)[1].split("void stop_ap_radio", 1)[0]

        self.assertIn("AP_RETRY_BACKOFF_INITIAL_MS = 1000", config)
        self.assertIn("AP_RETRY_BACKOFF_MAX_MS = 30000", config)
        self.assertLess(enable.index("ap_retry_ready(now_ms)"), enable.index("start_ap_radio"))
        self.assertIn("time_utils::deadline_reached", wifi)
        self.assertIn("schedule_ap_retry(millis(), failure_stage)", start)
        self.assertIn("const bool mode_ok = set_wifi_mode", start)
        self.assertIn("const bool config_ok = mode_ok", start)
        self.assertIn("const bool ok = config_ok", start)
        self.assertIn("reset_ap_retry();", start)

    def test_retry_diagnostics_are_visible_in_api_and_portal(self):
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")
        pages = (ROOT / "src/web/pages.cpp").read_text(encoding="utf-8")

        for field in (
            "ap_retry_schedule_count",
            "next_ap_retry_ms",
            "ap_retry_delay_ms",
            "ap_retry_scheduled",
            "ap_retry_remaining_ms",
            "last_ap_failure_stage",
        ):
            self.assertIn(f'wifiDiag["{field}"]', portal)
        self.assertIn("diag-next-ap-retry", pages)
        self.assertIn("diag-ap-failure-stage", pages)


if __name__ == "__main__":
    unittest.main()
