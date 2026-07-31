from datetime import date, timedelta
import math
from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
CONFIRMATIONS = 3
MAX_GAP_MS = 3000
DAY_MS = 86_400_000
MAGIC = 0x31594144
VERSION = 1
RECORD = struct.Struct("<IBBHII f I f H H I")


def valid_date(value):
    try:
        text = str(value)
        if len(text) != 8:
            return False
        parsed = date(int(text[:4]), int(text[4:6]), int(text[6:8]))
        return 2020 <= parsed.year <= 2099
    except (TypeError, ValueError):
        return False


def next_day(previous, current):
    if not valid_date(previous) or not valid_date(current):
        return False
    old = str(previous)
    new = str(current)
    return date(int(old[:4]), int(old[4:6]), int(old[6:8])) + timedelta(days=1) == date(
        int(new[:4]), int(new[4:6]), int(new[6:8])
    )


def generation_is_newer(candidate, reference):
    delta = (candidate - reference) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def encode_record(generation, day, distance_m, active_ms, max_speed_kph, update_min):
    prefix = RECORD.pack(
        MAGIC,
        VERSION,
        0,
        RECORD.size,
        generation & 0xFFFFFFFF,
        day,
        distance_m,
        active_ms,
        max_speed_kph,
        update_min,
        0,
        0,
    )
    crc = zlib.crc32(prefix[:-4]) & 0xFFFFFFFF
    return prefix[:-4] + struct.pack("<I", crc)


def decode_record(blob):
    if len(blob) != RECORD.size:
        return None
    fields = RECORD.unpack(blob)
    magic, version, flags, size, generation, day = fields[:6]
    distance_m, active_ms, max_speed_kph, update_min, reserved, stored_crc = fields[6:]
    if (
        magic != MAGIC
        or version != VERSION
        or flags != 0
        or size != RECORD.size
        or not valid_date(day)
        or not math.isfinite(distance_m)
        or distance_m < 0
        or not math.isfinite(max_speed_kph)
        or max_speed_kph < 0
        or update_min >= 1440
        or reserved != 0
        or zlib.crc32(blob[:-4]) & 0xFFFFFFFF != stored_crc
    ):
        return None
    return {
        "generation": generation,
        "date": day,
        "distance_m": distance_m,
        "active_ms": active_ms,
        "max_speed_kph": max_speed_kph,
        "update_min": update_min,
    }


def select_record(slots):
    decoded = [decode_record(blob) if blob is not None else None for blob in slots]
    if decoded[0] is None:
        return decoded[1]
    if decoded[1] is not None and generation_is_newer(
        decoded[1]["generation"], decoded[0]["generation"]
    ):
        return decoded[1]
    return decoded[0]


class DateGuardModel:
    def __init__(self, current=0):
        self.current = current
        self.metrics = {"distance_m": 0.0, "active_ms": 0, "max_speed_kph": 0.0}
        self.last_accepted_time = None
        self.pending_date = 0
        self.pending_time = 0
        self.pending_count = 0
        self.transitions = 0
        self.rejected = 0
        self.journal = []
        self.journal_ok = True

    def clear_pending(self, rejected=False):
        if rejected and self.pending_date:
            self.rejected += 1
        self.pending_date = 0
        self.pending_time = 0
        self.pending_count = 0

    def activate(self, observed, time_ms):
        if self.current:
            if not self.journal_ok:
                return False
            self.journal.append((self.current, self.metrics.copy()))
        had_current = bool(self.current)
        self.current = observed
        self.metrics = {"distance_m": 0.0, "active_ms": 0, "max_speed_kph": 0.0}
        self.transitions += int(had_current)
        return True

    def observe(self, observed, time_ms):
        if not valid_date(observed) or not 0 <= time_ms < DAY_MS:
            self.clear_pending(True)
            self.rejected += 1
            return False
        if not self.current:
            if not self.activate(observed, time_ms):
                return False
            self.last_accepted_time = time_ms
            self.clear_pending()
            return True
        if observed == self.current:
            self.clear_pending(True)
            if self.last_accepted_time is None or time_ms >= self.last_accepted_time:
                self.last_accepted_time = time_ms
            return True
        if observed < self.current:
            self.clear_pending(True)
            self.rejected += 1
            return False

        midnight_delta = None
        if self.last_accepted_time is not None and next_day(self.current, observed):
            midnight_delta = DAY_MS - self.last_accepted_time + time_ms
        if midnight_delta is not None and midnight_delta <= MAX_GAP_MS:
            if not self.activate(observed, time_ms):
                self.pending_date = observed
                self.pending_time = time_ms
                self.pending_count = CONFIRMATIONS
                return False
            self.last_accepted_time = time_ms
            self.clear_pending()
            return True

        if self.pending_date != observed:
            self.clear_pending(True)
            self.pending_date = observed
            self.pending_time = time_ms
            self.pending_count = 1
        elif time_ms > self.pending_time and time_ms - self.pending_time <= MAX_GAP_MS:
            self.pending_time = time_ms
            self.pending_count = min(CONFIRMATIONS, self.pending_count + 1)
        elif time_ms != self.pending_time:
            self.rejected += 1
            self.pending_time = time_ms
            self.pending_count = 1

        if self.pending_count < CONFIRMATIONS or not self.activate(observed, time_ms):
            return False
        self.last_accepted_time = time_ms
        self.clear_pending()
        return True


class DateTransitionTests(unittest.TestCase):
    def test_daily_record_round_trip_and_strict_validation(self):
        blob = encode_record(7, 20260731, 123.5, 456_000, 18.25, 719)
        decoded = decode_record(blob)
        self.assertEqual(decoded["generation"], 7)
        self.assertEqual(decoded["date"], 20260731)
        self.assertEqual(decoded["active_ms"], 456_000)
        self.assertIsNone(decode_record(blob[:-1]))
        damaged = bytearray(blob)
        damaged[20] ^= 0x01
        self.assertIsNone(decode_record(bytes(damaged)))
        self.assertIsNone(decode_record(encode_record(1, 20260230, 1, 1, 1, 1)))

    def test_ab_selection_survives_torn_write_and_generation_wrap(self):
        old = encode_record(0xFFFFFFFF, 20260730, 10, 1000, 2, 100)
        new = encode_record(0, 20260731, 20, 2000, 3, 200)
        self.assertEqual(select_record([old, new])["date"], 20260731)
        self.assertEqual(select_record([old, new[:10]])["date"], 20260730)

    def test_first_valid_date_initializes_without_a_completed_day(self):
        guard = DateGuardModel()
        self.assertTrue(guard.observe(20260731, 43_200_000))
        self.assertEqual(guard.current, 20260731)
        self.assertEqual(guard.journal, [])

    def test_single_forward_glitch_cannot_reset_metrics(self):
        guard = DateGuardModel(20260731)
        guard.metrics = {"distance_m": 812.0, "active_ms": 90_000, "max_speed_kph": 17.0}
        guard.observe(20260731, 43_200_000)
        self.assertFalse(guard.observe(20260801, 43_201_000))
        self.assertEqual(guard.current, 20260731)
        self.assertEqual(guard.metrics["distance_m"], 812.0)
        guard.observe(20260731, 43_202_000)
        self.assertEqual(guard.pending_count, 0)
        self.assertEqual(guard.journal, [])

    def test_three_consecutive_forward_observations_confirm_reacquisition(self):
        guard = DateGuardModel(20260701)
        guard.metrics["distance_m"] = 500.0
        for second in (10, 11):
            self.assertFalse(guard.observe(20260731, second * 1000))
        self.assertTrue(guard.observe(20260731, 12_000))
        self.assertEqual(guard.current, 20260731)
        self.assertEqual(guard.journal[0][0], 20260701)
        self.assertEqual(guard.journal[0][1]["distance_m"], 500.0)
        self.assertEqual(guard.metrics["distance_m"], 0.0)

    def test_backward_old_almanac_date_is_never_confirmed(self):
        guard = DateGuardModel(20260731)
        for second in range(10):
            self.assertFalse(guard.observe(20200101, second * 1000))
        self.assertEqual(guard.current, 20260731)
        self.assertEqual(guard.journal, [])

    def test_contiguous_midnight_is_immediate_across_calendar_boundaries(self):
        for previous, following in (
            (20260731, 20260801),
            (20261231, 20270101),
            (20240228, 20240229),
            (20240229, 20240301),
        ):
            guard = DateGuardModel(previous)
            guard.observe(previous, DAY_MS - 1000)
            self.assertTrue(guard.observe(following, 0), (previous, following))
            self.assertEqual(guard.pending_count, 0)
            self.assertEqual(guard.journal[-1][0], previous)

    def test_midnight_after_data_loss_requires_confirmation(self):
        guard = DateGuardModel(20260731)
        guard.observe(20260731, DAY_MS - 10_000)
        self.assertFalse(guard.observe(20260801, 0))
        self.assertFalse(guard.observe(20260801, 1000))
        self.assertTrue(guard.observe(20260801, 2000))

    def test_untrusted_or_stale_boundary_breaks_confirmation(self):
        guard = DateGuardModel(20260701)
        self.assertFalse(guard.observe(20260731, 10_000))
        self.assertFalse(guard.observe(20260731, 11_000))
        guard.clear_pending(True)  # Invalid/stale RMC boundary in firmware.
        self.assertFalse(guard.observe(20260731, 12_000))
        self.assertFalse(guard.observe(20260731, 13_000))
        self.assertTrue(guard.observe(20260731, 14_000))

    def test_journal_failure_blocks_reset_and_retries_without_data_loss(self):
        guard = DateGuardModel(20260731)
        guard.metrics["active_ms"] = 123_000
        guard.journal_ok = False
        for second in (10, 11, 12):
            self.assertFalse(guard.observe(20260802, second * 1000))
        self.assertEqual(guard.current, 20260731)
        self.assertEqual(guard.metrics["active_ms"], 123_000)
        self.assertEqual(guard.pending_count, CONFIRMATIONS)
        guard.journal_ok = True
        self.assertTrue(guard.observe(20260802, 13_000))
        self.assertEqual(guard.journal[-1][1]["active_ms"], 123_000)

    def test_firmware_contract_guards_every_date_dependent_metric(self):
        gps = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        gps_h = (ROOT / "include/gps/gps.h").read_text(encoding="utf-8")
        config = (ROOT / "include/config.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        self.assertIn("GPS_DATE_CONFIRM_OBSERVATIONS = 3", config)
        self.assertIn("bool accept_date_observation", gps)
        self.assertIn("date_yyyymmdd < current_date_yyyymmdd", gps)
        self.assertIn("midnight_delta_ms <= GPS_DATE_CONFIRM_MAX_GAP_MS", gps)
        self.assertIn("const bool metrics_usable = speed_usable && date_accepted", gps)
        stale_guard = gps.split("void expire_gps_if_stale", 1)[1].split(
            "uint16_t kph_to_cmps_u16_clamped", 1
        )[0]
        self.assertIn("clear_pending_date(true)", stale_guard)
        self.assertIn('is_sentence_type(nmea_line, "RMC")', gps)
        self.assertIn("if (had_current_day && !journal_current_day())", gps)
        self.assertIn('return slot == 0 ? "day_a" : "day_b"', gps)
        self.assertIn("daily_journal_crc", gps)
        self.assertIn("memcmp(&record, &readback, sizeof(record)) != 0", gps)
        self.assertIn("daily_journal_begin();", gps)
        self.assertIn("uint32_t date_pending_candidate();", gps_h)
        self.assertIn('gps["date_pending_candidate"]', portal)
        self.assertIn("append_last_completed_day_json(json)", gps)


if __name__ == "__main__":
    unittest.main()
