from datetime import date
from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
SESSION_VERSION = 1
GPS_FIX = 0x01
HAS_DATA = 0x02
IN_PROGRESS = 0x04
NO_FIX = 0x08
HISTORY_MAX = 3
STORE_MAGIC = 0x31534553
STORE_VERSION = 1
STORE_OPEN = 0x01
SUMMARY = struct.Struct("<BBIHIHIIHHBB")
STORE_HEADER = struct.Struct("<IBBHIBBH")
STORE_SIZE = STORE_HEADER.size + SUMMARY.size * 4 + 4


def valid_date(value):
    try:
        text = str(value)
        parsed = date(int(text[:4]), int(text[4:6]), int(text[6:8]))
        return len(text) == 8 and 2020 <= parsed.year <= 2099
    except (TypeError, ValueError):
        return False


def summary_checksum(blob):
    value = 0
    for byte in blob[:26]:
        value ^= byte
    return value


def encode_summary(
    flags=0,
    start_date=0,
    start_min=0,
    end_date=0,
    end_min=0,
    distance_m=0,
    active_s=0,
    avg_cmps=0,
    max_cmps=0,
):
    blob = SUMMARY.pack(
        SESSION_VERSION,
        flags,
        start_date,
        start_min,
        end_date,
        end_min,
        distance_m,
        active_s,
        avg_cmps,
        max_cmps,
        0,
        0,
    )
    return blob[:26] + bytes((summary_checksum(blob), 0))


def blank_summary(open_session=False):
    return encode_summary(IN_PROGRESS if open_session else 0)


def data_summary(distance=120, active_s=60, in_progress=True, day=20260731):
    avg = min(distance * 100 // active_s, 65535) if active_s else 0
    flags = GPS_FIX | HAS_DATA | (IN_PROGRESS if in_progress else 0)
    return encode_summary(flags, day, 600, day, 601, distance, active_s, avg, 500)


def decode_summary(blob):
    if len(blob) != SUMMARY.size:
        return None
    fields = SUMMARY.unpack(blob)
    version, flags = fields[:2]
    start_date, start_min, end_date, end_min = fields[2:6]
    distance_m, active_s, avg_cmps, max_cmps, checksum, pad = fields[6:]
    if (
        version != SESSION_VERSION
        or flags & ~(GPS_FIX | HAS_DATA | IN_PROGRESS | NO_FIX)
        or pad != 0
        or checksum != summary_checksum(blob)
    ):
        return None
    payload_zero = not any(
        (start_date, start_min, end_date, end_min, distance_m, active_s, avg_cmps, max_cmps)
    )
    has_fix = bool(flags & GPS_FIX)
    if not has_fix:
        if flags not in (0, IN_PROGRESS, NO_FIX) or not payload_zero:
            return None
    else:
        expected_avg = min(distance_m * 100 // active_s, 65535) if active_s else 0
        if (
            not flags & HAS_DATA
            or flags & NO_FIX
            or not valid_date(start_date)
            or not valid_date(end_date)
            or start_min >= 1440
            or end_min >= 1440
            or (start_date, start_min) > (end_date, end_min)
            or avg_cmps != expected_avg
        ):
            return None
    return {
        "flags": flags,
        "distance_m": distance_m,
        "active_s": active_s,
        "blob": blob,
    }


def finalize_summary(blob):
    decoded = decode_summary(blob)
    if decoded is None:
        return None
    if decoded["flags"] & GPS_FIX:
        fields = list(SUMMARY.unpack(blob))
        fields[1] &= ~IN_PROGRESS
        return encode_summary(
            fields[1], fields[2], fields[3], fields[4], fields[5],
            fields[6], fields[7], fields[8], fields[9]
        )
    return encode_summary(NO_FIX)


def generation_is_newer(candidate, reference):
    delta = (candidate - reference) & 0xFFFFFFFF
    return 0 < delta < 0x80000000


def encode_store(generation, history, count, idx, current, open_session):
    header = STORE_HEADER.pack(
        STORE_MAGIC,
        STORE_VERSION,
        STORE_OPEN if open_session else 0,
        STORE_SIZE,
        generation & 0xFFFFFFFF,
        count,
        idx,
        0,
    )
    body = header + b"".join(history) + current
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def decode_store(blob):
    if blob is None or len(blob) != STORE_SIZE:
        return None
    magic, version, flags, size, generation, count, idx, reserved = STORE_HEADER.unpack_from(blob)
    if (
        magic != STORE_MAGIC
        or version != STORE_VERSION
        or flags & ~STORE_OPEN
        or size != STORE_SIZE
        or reserved != 0
        or count > HISTORY_MAX
        or idx >= HISTORY_MAX
        or (count < HISTORY_MAX and idx != count)
        or zlib.crc32(blob[:-4]) & 0xFFFFFFFF != struct.unpack_from("<I", blob, STORE_SIZE - 4)[0]
    ):
        return None
    offset = STORE_HEADER.size
    history = []
    for index in range(HISTORY_MAX):
        summary = blob[offset : offset + SUMMARY.size]
        offset += SUMMARY.size
        decoded = decode_summary(summary)
        used = count == HISTORY_MAX or index < count
        if decoded is None or (used and decoded["flags"] & IN_PROGRESS):
            return None
        if used and not decoded["flags"] & (GPS_FIX | NO_FIX):
            return None
        if not used and decoded["flags"] != 0:
            return None
        history.append(summary)
    current = blob[offset : offset + SUMMARY.size]
    decoded_current = decode_summary(current)
    open_session = bool(flags & STORE_OPEN)
    if decoded_current is None:
        return None
    if open_session != bool(decoded_current["flags"] & IN_PROGRESS):
        return None
    if not open_session and decoded_current["flags"] != 0:
        return None
    return {
        "generation": generation,
        "history": history,
        "count": count,
        "idx": idx,
        "current": current,
        "open": open_session,
    }


def select_store(slots):
    decoded = [decode_store(blob) for blob in slots]
    if decoded[0] is None:
        return decoded[1]
    if decoded[1] is not None and generation_is_newer(
        decoded[1]["generation"], decoded[0]["generation"]
    ):
        return decoded[1]
    return decoded[0]


def push_history(state, summary):
    history = list(state["history"])
    history[state["idx"]] = summary
    count = min(state["count"] + 1, HISTORY_MAX)
    idx = (state["idx"] + 1) % HISTORY_MAX
    return history, count, idx


def recover_and_open(state):
    history, count, idx = state["history"], state["count"], state["idx"]
    if state["open"]:
        history, count, idx = push_history(state, finalize_summary(state["current"]))
    return encode_store(
        state["generation"] + 1,
        history,
        count,
        idx,
        blank_summary(True),
        True,
    )


class SessionPersistenceTests(unittest.TestCase):
    def empty_store(self, generation=1):
        return encode_store(
            generation,
            [blank_summary() for _ in range(HISTORY_MAX)],
            0,
            0,
            blank_summary(True),
            True,
        )

    def test_record_round_trip_and_semantic_validation(self):
        blob = self.empty_store()
        self.assertEqual(decode_store(blob)["count"], 0)
        self.assertIsNone(decode_store(blob[:-1]))
        corrupt = bytearray(blob)
        corrupt[40] ^= 0x20
        self.assertIsNone(decode_store(bytes(corrupt)))
        invalid_avg = encode_summary(GPS_FIX | HAS_DATA, 20260731, 1, 20260731, 2, 100, 10, 999, 1)
        self.assertIsNone(decode_summary(invalid_avg))

    def test_torn_inactive_write_preserves_previous_open_session(self):
        previous = self.empty_store(10)
        state = decode_store(previous)
        candidate = recover_and_open(state)
        self.assertEqual(select_store([previous, candidate[:60]])["generation"], 10)

    def test_power_cut_before_commit_cannot_duplicate_recovered_session(self):
        original = decode_store(
            encode_store(4, [blank_summary() for _ in range(3)], 0, 0, data_summary(), True)
        )
        candidate = recover_and_open(original)
        self.assertEqual(select_store([encode_store(4, original["history"], 0, 0, original["current"], True), candidate[:70]])["count"], 0)
        committed = decode_store(candidate)
        data_entries = [decode_summary(item)["flags"] & GPS_FIX for item in committed["history"]]
        self.assertEqual(sum(bool(value) for value in data_entries), 1)

    def test_power_cut_after_commit_keeps_exactly_one_data_session(self):
        original_blob = encode_store(
            7, [blank_summary() for _ in range(3)], 0, 0, data_summary(), True
        )
        committed_blob = recover_and_open(decode_store(original_blob))
        selected = select_store([original_blob, committed_blob])
        self.assertEqual(selected["count"], 1)
        self.assertEqual(sum(bool(decode_summary(x)["flags"] & GPS_FIX) for x in selected["history"]), 1)

    def test_ring_retains_latest_three_complete_sessions(self):
        state = decode_store(self.empty_store())
        for distance in (10, 20, 30, 40, 50):
            complete = finalize_summary(data_summary(distance=distance))
            history, count, idx = push_history(state, complete)
            state = decode_store(encode_store(state["generation"] + 1, history, count, idx, blank_summary(True), True))
        distances = sorted(
            decode_summary(item)["distance_m"] for item in state["history"]
        )
        self.assertEqual(distances, [30, 40, 50])

    def test_generation_selection_crosses_uint32_wrap(self):
        old = self.empty_store(0xFFFFFFFF)
        new = self.empty_store(0)
        self.assertEqual(select_store([old, new])["generation"], 0)

    def test_firmware_uses_one_verified_store_and_no_split_writes(self):
        gps = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/gps/gps.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        for required in (
            "struct SessionStoreRecord",
            'return slot == 0 ? "ses_a" : "ses_b"',
            "record.crc32 == session_store_crc(record)",
            "memcmp(&record, &readback, sizeof(record)) != 0",
            'prefs.getUChar("ses_mig", 0)',
            "session_store_generation_is_newer",
            "session_store_mirror_degraded",
            "session_store_recoveries++",
        ):
            self.assertIn(required, gps)
        for forbidden in (
            'prefs.putBytes("s_cur"',
            'prefs.putUChar("s_open"',
            'prefs.putUChar("h_cnt"',
            'prefs.putUChar("h_idx"',
            'prefs.putUChar("h_ver"',
        ):
            self.assertNotIn(forbidden, gps)
        self.assertIn("uint32_t session_storage_save_failures();", header)
        self.assertIn("uint8_t session_history_count();", header)
        self.assertIn('gps["session_storage"]', portal)
        self.assertIn('sessionStorage["recoveries"]', portal)


if __name__ == "__main__":
    unittest.main()
