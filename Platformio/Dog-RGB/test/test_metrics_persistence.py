from datetime import date
import math
from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x3154454D
VERSION = 1
RECORD = struct.Struct("<IBBHII f I f H H I")


def valid_date(value):
    try:
        text = str(value)
        parsed = date(int(text[:4]), int(text[4:6]), int(text[6:8]))
        return len(text) == 8 and 2020 <= parsed.year <= 2099
    except (TypeError, ValueError):
        return False


def valid_values(day, distance_m, active_ms, max_speed_kph, update_min):
    if (
        not math.isfinite(distance_m)
        or distance_m < 0
        or not math.isfinite(max_speed_kph)
        or max_speed_kph < 0
        or not 0 <= update_min < 1440
    ):
        return False
    if day == 0:
        return distance_m == 0 and active_ms == 0 and max_speed_kph == 0 and update_min == 0
    return valid_date(day)


def encode_record(generation, day, distance_m, active_ms, max_speed_kph, update_min):
    blob = RECORD.pack(
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
    return blob[:-4] + struct.pack("<I", zlib.crc32(blob[:-4]) & 0xFFFFFFFF)


def decode_record(blob):
    if blob is None or len(blob) != RECORD.size:
        return None
    fields = RECORD.unpack(blob)
    magic, version, flags, size, generation, day = fields[:6]
    distance_m, active_ms, max_speed_kph, update_min, reserved, stored_crc = fields[6:]
    if (
        magic != MAGIC
        or version != VERSION
        or flags != 0
        or size != RECORD.size
        or reserved != 0
        or not valid_values(day, distance_m, active_ms, max_speed_kph, update_min)
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


def generation_is_newer(candidate, reference):
    delta = (candidate - reference) & 0xFFFFFFFF
    return 0 < delta < 0x80000000


def select_record(slots):
    decoded = [decode_record(blob) for blob in slots]
    if decoded[0] is None:
        return decoded[1]
    if decoded[1] is not None and generation_is_newer(
        decoded[1]["generation"], decoded[0]["generation"]
    ):
        return decoded[1]
    return decoded[0]


def reconcile_completed_day(completed_date, live):
    if live is not None and live["date"] != 0 and live["date"] <= completed_date:
        return decode_record(encode_record(live["generation"] + 1, 0, 0, 0, 0, 0))
    return live


class MetricsPersistenceTests(unittest.TestCase):
    def test_record_round_trip_and_strict_field_validation(self):
        blob = encode_record(7, 20260731, 812.5, 91_000, 18.25, 719)
        decoded = decode_record(blob)
        self.assertEqual(decoded["generation"], 7)
        self.assertEqual(decoded["active_ms"], 91_000)
        self.assertIsNone(decode_record(blob[:-1]))
        damaged = bytearray(blob)
        damaged[20] ^= 0x40
        self.assertIsNone(decode_record(bytes(damaged)))
        self.assertIsNone(decode_record(encode_record(1, 20260230, 1, 1, 1, 1)))

    def test_empty_pre_fix_state_is_valid_but_cannot_contain_metrics(self):
        self.assertIsNotNone(decode_record(encode_record(0, 0, 0, 0, 0, 0)))
        self.assertIsNone(decode_record(encode_record(0, 0, 1, 0, 0, 0)))

    def test_torn_or_corrupt_inactive_write_preserves_previous_snapshot(self):
        previous = encode_record(10, 20260731, 500, 60_000, 12, 700)
        candidate = encode_record(11, 20260801, 0, 0, 0, 0)
        self.assertEqual(select_record([previous, candidate[:18]])["date"], 20260731)
        corrupt = bytearray(candidate)
        corrupt[-6] ^= 0x01
        self.assertEqual(select_record([previous, bytes(corrupt)])["date"], 20260731)

    def test_snapshot_never_selects_a_mixture_of_days(self):
        old = encode_record(20, 20260731, 999, 120_000, 20, 1439)
        new = encode_record(21, 20260801, 3, 1_000, 4, 1)
        selected = select_record([old, new])
        self.assertEqual(
            (selected["date"], selected["distance_m"], selected["active_ms"]),
            (20260801, 3, 1_000),
        )

    def test_generation_selection_crosses_uint32_wrap(self):
        old = encode_record(0xFFFFFFFF, 20260731, 10, 1_000, 2, 100)
        new = encode_record(0, 20260801, 20, 2_000, 3, 200)
        self.assertEqual(select_record([old, new])["date"], 20260801)

    def test_completed_day_journal_rejects_stale_live_snapshot(self):
        completed_date = 20260731
        stale = decode_record(encode_record(4, completed_date, 900, 80_000, 19, 1439))
        current = decode_record(encode_record(5, 20260801, 2, 1_000, 3, 1))
        recovered = reconcile_completed_day(completed_date, stale)
        self.assertEqual(
            (recovered["date"], recovered["distance_m"], recovered["active_ms"]),
            (0, 0, 0),
        )
        self.assertEqual(reconcile_completed_day(completed_date, current), current)

    def test_firmware_contract_uses_verified_ab_records_and_one_time_migration(self):
        gps = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/gps/gps.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        for required in (
            "struct MetricsRecord",
            'return slot == 0 ? "met_a" : "met_b"',
            "record.crc32 == metrics_record_crc(record)",
            "memcmp(&record, &readback, sizeof(record)) != 0",
            'prefs.getUChar("met_mig", 0)',
            "metrics_generation_is_newer",
            "metrics_mirror_degraded",
            "reconcile_metrics_with_daily_journal();",
        ):
            self.assertIn(required, gps)
        for forbidden in (
            'prefs.putUInt("date"',
            'prefs.putFloat("dist_m"',
            'prefs.putULong("active_ms"',
            'prefs.putFloat("max_kph"',
            'prefs.putUShort("upd_min"',
        ):
            self.assertNotIn(forbidden, gps)
        self.assertIn("uint32_t metrics_storage_save_failures();", header)
        self.assertIn('gps["metrics_storage"]', portal)
        self.assertIn('metricsStorage["recoveries"]', portal)


if __name__ == "__main__":
    unittest.main()
