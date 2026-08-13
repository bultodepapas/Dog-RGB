from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x43475244
RECORD_VERSION = 2
SCHEMA_VERSION = 6
HEADER = struct.Struct("<IHHIB")
CRC = struct.Struct("<I")


def encode_record(generation, payload):
    size = HEADER.size + len(payload) + CRC.size
    body = HEADER.pack(MAGIC, RECORD_VERSION, size, generation, SCHEMA_VERSION) + payload
    return body + CRC.pack(zlib.crc32(body) & 0xFFFFFFFF)


def decode_record(blob):
    if blob is None or len(blob) < HEADER.size + CRC.size:
        return None
    magic, record_version, size, generation, schema_version = HEADER.unpack_from(blob)
    if (
        magic != MAGIC
        or record_version != RECORD_VERSION
        or schema_version != SCHEMA_VERSION
        or size != len(blob)
    ):
        return None
    stored_crc = CRC.unpack_from(blob, len(blob) - CRC.size)[0]
    if zlib.crc32(blob[:-CRC.size]) & 0xFFFFFFFF != stored_crc:
        return None
    return generation, blob[HEADER.size : -CRC.size]


def generation_is_newer(candidate, reference):
    delta = (candidate - reference) & 0xFFFFFFFF
    return 0 < delta < 0x80000000


class ConfigStoreModel:
    def __init__(self):
        self.slots = [None, None]
        self.active = None
        self.generation = 0

    def boot(self):
        valid = [decode_record(blob) for blob in self.slots]
        if valid[0] is None and valid[1] is None:
            self.active = None
            self.generation = 0
            return None
        selected = 0
        if valid[0] is None or (
            valid[1] is not None and generation_is_newer(valid[1][0], valid[0][0])
        ):
            selected = 1
        self.active = selected
        self.generation = valid[selected][0]
        return valid[selected][1]

    def save(self, payload, fault=None):
        target = 1 if self.active == 0 else 0
        generation = (self.generation + 1) & 0xFFFFFFFF
        if generation == 0:
            generation = 1
        blob = encode_record(generation, payload)
        if fault == "truncate":
            blob = blob[: len(blob) // 2]
        elif fault == "bitflip":
            blob = bytearray(blob)
            blob[-5] ^= 0x80
            blob = bytes(blob)
        self.slots[target] = blob
        if fault is not None or decode_record(self.slots[target]) != (generation, payload):
            return False
        self.active = target
        self.generation = generation
        return True


class ConfigPersistenceTests(unittest.TestCase):
    def test_valid_record_round_trip(self):
        payload = b"brightness=77;mode=speed;ssid=DogRGB"
        self.assertEqual(decode_record(encode_record(42, payload)), (42, payload))

    def test_corruption_and_partial_writes_are_rejected(self):
        blob = encode_record(7, b"complete configuration")
        corrupt = bytearray(blob)
        corrupt[HEADER.size + 3] ^= 0x20
        self.assertIsNone(decode_record(bytes(corrupt)))
        self.assertIsNone(decode_record(blob[:-1]))
        self.assertIsNone(decode_record(blob + b"\x00"))

    def test_interrupted_update_falls_back_to_previous_complete_generation(self):
        store = ConfigStoreModel()
        self.assertTrue(store.save(b"factory defaults"))
        self.assertTrue(store.save(b"owner settings"))
        self.assertFalse(store.save(b"new settings", fault="truncate"))

        self.assertEqual(store.boot(), b"owner settings")

    def test_interrupted_factory_reset_does_not_erase_owner_settings(self):
        store = ConfigStoreModel()
        self.assertTrue(store.save(b"older owner settings"))
        self.assertTrue(store.save(b"current owner settings"))
        self.assertFalse(store.save(b"factory defaults", fault="bitflip"))

        self.assertEqual(store.boot(), b"current owner settings")

    def test_successful_saves_alternate_slots_and_select_newest(self):
        store = ConfigStoreModel()
        for index in range(1, 8):
            previous_slot = store.active
            self.assertTrue(store.save(f"generation {index}".encode()))
            if previous_slot is not None:
                self.assertNotEqual(store.active, previous_slot)
        self.assertEqual(store.boot(), b"generation 7")

    def test_generation_order_is_wrap_safe(self):
        self.assertTrue(generation_is_newer(1, 0xFFFFFFFF))
        self.assertFalse(generation_is_newer(0xFFFFFFFF, 1))
        self.assertFalse(generation_is_newer(10, 10))

    def test_firmware_contract_uses_verified_ab_records_and_transactional_routes(self):
        config_cpp = (ROOT / "src/config/runtime_config.cpp").read_text(encoding="utf-8")
        config_h = (ROOT / "include/config/runtime_config.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        for required in (
            'CONFIG_RECORD_KEYS[2] = {"cfg_a", "cfg_b"}',
            "struct __attribute__((packed)) ConfigRecord",
            "struct __attribute__((packed)) ConfigRecordV5",
            "CONFIG_SCHEMA_VERSION_V5 = 5",
            "migrated_v5[selected]",
            "record.record_size != sizeof(record)",
            "record.crc32 != config_record_crc(record)",
            "prefs_cfg.putBytes(key, &record, sizeof(record)) != sizeof(record)",
            "memcmp(&record, &readback, sizeof(record)) != 0",
            "generation_is_newer",
            "finish_legacy_migration",
            "if (save() && save())",
        ):
            self.assertIn(required, config_cpp)
        self.assertIn("bool save();", config_h)
        self.assertNotIn("prefs_cfg().clear()", portal)
        # Verify each current RuntimeConfig mutation route instead of relying
        # on a global occurrence count, which breaks whenever unrelated route
        # layout changes.
        route_bounds = (
            ("void handle_config_post()", "void handle_config_reset()"),
            ("void handle_config_reset()", "void handle_wifi_ap_save()"),
            ("void handle_wifi_ap_save()", "void handle_config_page()"),
        )
        for start_marker, end_marker in route_bounds:
            start = portal.index(start_marker)
            end = portal.index(end_marker, start + len(start_marker))
            self.assertIn(
                "persist_config_or_restore(previous)",
                portal[start:end],
                start_marker,
            )
        self.assertIn(r'\"reason\":\"storage\"', portal)
        self.assertIn('configStorage["generation"]', portal)
        self.assertIn('configStorage["save_failures"]', portal)


if __name__ == "__main__":
    unittest.main()
