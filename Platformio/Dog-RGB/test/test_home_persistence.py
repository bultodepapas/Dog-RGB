import math
from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x454D4F48
VERSION = 1
BODY = struct.Struct("<IHHIBBHff")
CRC = struct.Struct("<I")


def valid_state(is_set, source, lat, lon):
    if not is_set:
        return source == 0 and lat == 0.0 and lon == 0.0
    return (
        source in (1, 2)
        and math.isfinite(lat)
        and math.isfinite(lon)
        and -90.0 <= lat <= 90.0
        and -180.0 <= lon <= 180.0
    )


def encode_home(generation, is_set, source, lat, lon):
    body = BODY.pack(MAGIC, VERSION, BODY.size + CRC.size, generation, is_set, source, 0, lat, lon)
    return body + CRC.pack(zlib.crc32(body) & 0xFFFFFFFF)


def decode_home(blob):
    if blob is None or len(blob) != BODY.size + CRC.size:
        return None
    magic, version, size, generation, is_set, source, reserved, lat, lon = BODY.unpack_from(blob)
    stored_crc = CRC.unpack_from(blob, BODY.size)[0]
    if (
        magic != MAGIC
        or version != VERSION
        or size != len(blob)
        or is_set > 1
        or reserved != 0
        or zlib.crc32(blob[: BODY.size]) & 0xFFFFFFFF != stored_crc
        or not valid_state(bool(is_set), source, lat, lon)
    ):
        return None
    return generation, bool(is_set), source, lat, lon


def newer(candidate, reference):
    delta = (candidate - reference) & 0xFFFFFFFF
    return 0 < delta < 0x80000000


class HomeStoreModel:
    def __init__(self):
        self.slots = [None, None]
        self.active = None
        self.generation = 0
        self.state = (False, 0, 0.0, 0.0)

    def boot(self):
        valid = [decode_home(blob) for blob in self.slots]
        if valid[0] is None and valid[1] is None:
            self.active = None
            self.generation = 0
            self.state = (False, 0, 0.0, 0.0)
            return self.state
        selected = 0
        if valid[0] is None or (valid[1] is not None and newer(valid[1][0], valid[0][0])):
            selected = 1
        generation, is_set, source, lat, lon = valid[selected]
        self.active = selected
        self.generation = generation
        self.state = (is_set, source, lat, lon)
        return self.state

    def save(self, state, fault=None):
        is_set, source, lat, lon = state
        if not valid_state(is_set, source, lat, lon):
            return False
        target = 1 if self.active == 0 else 0
        generation = 1 if self.generation == 0xFFFFFFFF else self.generation + 1
        blob = encode_home(generation, int(is_set), source, lat, lon)
        if fault == "truncate":
            blob = blob[:-5]
        elif fault == "bitflip":
            blob = bytearray(blob)
            blob[-7] ^= 0x10
            blob = bytes(blob)
        self.slots[target] = blob
        if fault is not None or decode_home(blob) is None:
            return False
        self.active = target
        self.generation = generation
        self.state = state
        return True


class HomePersistenceTests(unittest.TestCase):
    def test_set_and_unset_records_round_trip(self):
        manual = decode_home(encode_home(5, 1, 2, 4.711, -74.0721))
        self.assertEqual(manual[:3], (5, True, 2))
        self.assertAlmostEqual(manual[3], 4.711, places=5)
        self.assertAlmostEqual(manual[4], -74.0721, places=5)
        self.assertEqual(decode_home(encode_home(6, 0, 0, 0.0, 0.0)), (6, False, 0, 0.0, 0.0))

    def test_strict_validation_rejects_invalid_coordinates_sources_and_nan(self):
        for state in (
            (True, 0, 4.7, -74.0),
            (True, 3, 4.7, -74.0),
            (True, 2, 90.0001, -74.0),
            (True, 2, 4.7, 180.0001),
            (True, 2, math.nan, -74.0),
            (False, 1, 0.0, 0.0),
            (False, 0, 4.7, -74.0),
        ):
            self.assertFalse(valid_state(*state), state)

    def test_crc_length_and_reserved_field_are_enforced(self):
        blob = encode_home(9, 1, 1, 4.7, -74.0)
        corrupt = bytearray(blob)
        corrupt[-6] ^= 0x01
        self.assertIsNone(decode_home(bytes(corrupt)))
        self.assertIsNone(decode_home(blob[:-1]))
        self.assertIsNone(decode_home(blob + b"\x00"))

        fields = list(BODY.unpack_from(blob))
        fields[6] = 1
        body = BODY.pack(*fields)
        self.assertIsNone(decode_home(body + CRC.pack(zlib.crc32(body) & 0xFFFFFFFF)))

    def test_interrupted_manual_set_retains_previous_home(self):
        store = HomeStoreModel()
        old_home = (True, 2, 4.711, -74.0721)
        self.assertTrue(store.save((False, 0, 0.0, 0.0)))
        self.assertTrue(store.save(old_home))
        self.assertFalse(store.save((True, 2, 4.72, -74.08), fault="truncate"))
        loaded = store.boot()
        self.assertEqual(loaded[:2], old_home[:2])
        self.assertAlmostEqual(loaded[2], old_home[2], places=5)
        self.assertAlmostEqual(loaded[3], old_home[3], places=5)

    def test_interrupted_clear_does_not_create_false_unset_state(self):
        store = HomeStoreModel()
        owner_home = (True, 2, 4.711, -74.0721)
        self.assertTrue(store.save((True, 1, 4.70, -74.0)))
        self.assertTrue(store.save(owner_home))
        self.assertFalse(store.save((False, 0, 0.0, 0.0), fault="bitflip"))
        loaded = store.boot()
        self.assertTrue(loaded[0])
        self.assertEqual(loaded[1], 2)

    def test_failed_auto_set_remains_unset_and_can_retry(self):
        store = HomeStoreModel()
        self.assertTrue(store.save((False, 0, 0.0, 0.0)))
        previous = store.state
        self.assertFalse(store.save((True, 1, 4.711, -74.0721), fault="truncate"))
        self.assertEqual(store.state, previous)
        self.assertTrue(store.save((True, 1, 4.711, -74.0721)))
        self.assertTrue(store.state[0])

    def test_firmware_contract_uses_verified_ab_home_records(self):
        home = (ROOT / "src/geofence/home.cpp").read_text(encoding="utf-8")
        header = (ROOT / "include/geofence/home.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        for required in (
            'HOME_RECORD_KEYS[2] = {"home_a", "home_b"}',
            "struct __attribute__((packed)) HomeRecord",
            "static_assert(sizeof(HomeRecord) == 28",
            "record.crc32 != home_record_crc(record)",
            "valid_home_state(record.is_set != 0",
            "prefs_cfg.putBytes(key, &record, sizeof(record)) != sizeof(record)",
            "memcmp(&record, &readback, sizeof(record)) != 0",
            "if (save_home() && save_home())",
            "HOME_BLOB_MIGRATED_KEY",
        ):
            self.assertIn(required, home)
        self.assertIn("bool set_home", header)
        self.assertIn("bool clear_home", header)
        self.assertIn("home_set_state = previous_set", home)
        self.assertIn('homeStorage["save_failures"]', portal)
        self.assertEqual(portal.count("!geofence::set_home"), 1)
        self.assertEqual(portal.count("!geofence::clear_home"), 1)


if __name__ == "__main__":
    unittest.main()
