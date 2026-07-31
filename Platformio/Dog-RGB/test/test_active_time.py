from datetime import date, timedelta
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAX_GAP_MS = 3000
UINT32_MAX = 0xFFFFFFFF


def parse_time_ms(value):
    if len(value) < 6 or not value[:6].isdigit():
        return None
    if len(value) > 6:
        if value[6] != "." or not value[7:].isdigit():
            return None
    hour, minute, second = int(value[:2]), int(value[2:4]), int(value[4:6])
    if hour > 23 or minute > 59 or second > 59:
        return None
    fraction = (value[7:10] + "000")[:3] if len(value) > 7 else "000"
    return ((hour * 60 + minute) * 60 + second) * 1000 + int(fraction)


def parse_date(value):
    text = str(value)
    return date(int(text[:4]), int(text[4:6]), int(text[6:8]))


class ActiveTimeModel:
    def __init__(self):
        self.previous = None
        self.active_ms = 0
        self.session_active_ms = 0
        self.intervals = 0
        self.rejected = 0
        self.last_delta_ms = 0

    def observe(self, day, time_ms, active):
        if self.previous is not None:
            previous_day, previous_ms, previous_active = self.previous
            if day != previous_day:
                self.active_ms = 0
            duplicate = day == previous_day and time_ms == previous_ms
            delta = None
            if day == previous_day and time_ms > previous_ms:
                delta = time_ms - previous_ms
            elif parse_date(day) == parse_date(previous_day) + timedelta(days=1):
                delta = 86_400_000 - previous_ms + time_ms

            if delta is not None and delta <= MAX_GAP_MS:
                self.intervals += 1
                self.last_delta_ms = delta
                if previous_active and active:
                    daily_delta = delta if day == previous_day else time_ms
                    self.active_ms = min(UINT32_MAX, self.active_ms + daily_delta)
                    self.session_active_ms = min(UINT32_MAX, self.session_active_ms + delta)
            elif not duplicate:
                self.rejected += 1
                self.last_delta_ms = 0
        self.previous = (day, time_ms, active)


class ActiveTimeTests(unittest.TestCase):
    def test_nmea_time_parser_preserves_milliseconds(self):
        self.assertEqual(parse_time_ms("000000"), 0)
        self.assertEqual(parse_time_ms("123456.7"), 45_296_700)
        self.assertEqual(parse_time_ms("123456.78"), 45_296_780)
        self.assertEqual(parse_time_ms("235959.9999"), 86_399_999)
        for invalid in ("", "12345", "246000", "126060", "123456.", "123456x1", "12aa56"):
            self.assertIsNone(parse_time_ms(invalid), invalid)

    def test_regular_and_jittered_intervals_use_real_elapsed_time(self):
        counter = ActiveTimeModel()
        counter.observe(20260731, 10_000, True)
        counter.observe(20260731, 11_000, True)
        counter.observe(20260731, 12_075, True)
        counter.observe(20260731, 13_020, True)
        self.assertEqual(counter.active_ms, 3020)
        self.assertEqual(counter.intervals, 3)

    def test_ten_seconds_of_buffered_one_hz_observations_are_not_lost(self):
        counter = ActiveTimeModel()
        # These may all be parsed in one loop after a network/flash stall. Their
        # GNSS timestamps, rather than parser wall time, preserve every interval.
        for second in range(11):
            counter.observe(20260731, second * 1000, True)
        self.assertEqual(counter.active_ms, 10_000)
        self.assertEqual(counter.intervals, 10)
        self.assertEqual(counter.rejected, 0)

    def test_lone_fix_after_long_outage_does_not_invent_activity(self):
        counter = ActiveTimeModel()
        counter.observe(20260731, 0, True)
        counter.observe(20260731, 10_000, True)
        self.assertEqual(counter.active_ms, 0)
        self.assertEqual(counter.rejected, 1)
        counter.observe(20260731, 11_000, True)
        self.assertEqual(counter.active_ms, 1000)

    def test_interval_requires_active_evidence_at_both_ends(self):
        counter = ActiveTimeModel()
        for second, active in enumerate((False, True, True, False, False, True, True)):
            counter.observe(20260731, second * 1000, active)
        self.assertEqual(counter.active_ms, 2000)

    def test_midnight_month_year_and_leap_day_are_contiguous(self):
        for previous_day, next_day in (
            (20260731, 20260801),
            (20261231, 20270101),
            (20240228, 20240229),
            (20240229, 20240301),
        ):
            counter = ActiveTimeModel()
            counter.observe(previous_day, 86_399_000, True)
            counter.observe(next_day, 0, True)
            self.assertEqual(counter.active_ms, 0, (previous_day, next_day))
            self.assertEqual(counter.session_active_ms, 1000, (previous_day, next_day))
            counter.observe(next_day, 1000, True)
            self.assertEqual(counter.active_ms, 1000, (previous_day, next_day))
            self.assertEqual(counter.session_active_ms, 2000, (previous_day, next_day))

    def test_daily_and_session_counters_saturate_instead_of_wrapping(self):
        counter = ActiveTimeModel()
        counter.observe(20260731, 0, True)
        counter.active_ms = UINT32_MAX - 500
        counter.session_active_ms = UINT32_MAX - 500
        counter.observe(20260731, 1000, True)
        self.assertEqual(counter.active_ms, UINT32_MAX)
        self.assertEqual(counter.session_active_ms, UINT32_MAX)

    def test_duplicate_is_ignored_and_backward_time_rebaselines(self):
        counter = ActiveTimeModel()
        counter.observe(20260731, 5000, True)
        counter.observe(20260731, 5000, True)
        self.assertEqual(counter.rejected, 0)
        counter.observe(20260731, 4000, True)
        self.assertEqual(counter.rejected, 1)
        counter.observe(20260731, 5000, True)
        self.assertEqual(counter.active_ms, 1000)

    def test_firmware_contract_uses_observation_time_and_bounded_gaps(self):
        gps = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        gps_h = (ROOT / "include/gps/gps.h").read_text(encoding="utf-8")
        config = (ROOT / "include/config.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        self.assertIn("GPS_ACTIVE_MAX_GAP_MS = 3000", config)
        self.assertIn("bool parse_time_of_day_ms", gps)
        self.assertIn("uint32_t *time_ms_of_day", gps)
        self.assertIn("void update_active_time_observation", gps)
        accounting = gps.split("void update_active_time_observation", 1)[1].split(
            "void reset_distance_baseline", 1
        )[0]
        self.assertIn("delta_ms <= GPS_ACTIVE_MAX_GAP_MS", accounting)
        self.assertIn("last_activity_observation_active && active", accounting)
        self.assertIn("daily_delta_ms", accounting)
        self.assertIn("saturating_add_active_time", accounting)
        self.assertIn("date_is_next_day", accounting)
        self.assertNotIn("millis()", accounting)
        self.assertNotIn("active_time_ms_val += GPS_SAMPLE_MS", gps)
        self.assertIn("uint32_t activity_gap_rejects();", gps_h)
        self.assertIn('gps["activity_gap_rejects"]', portal)


if __name__ == "__main__":
    unittest.main()
