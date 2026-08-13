from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x49465744
VERSION = 1
RECORD = struct.Struct("<IHHIBBBB33s65sI")


def encode_record(generation, ssid, password):
    ssid_bytes = ssid.encode("utf-8")
    pass_bytes = password.encode("utf-8")
    configured = bool(ssid_bytes)
    if len(ssid_bytes) > 32 or len(pass_bytes) > 64 or (not configured and pass_bytes):
        return None
    body = RECORD.pack(
        MAGIC,
        VERSION,
        RECORD.size,
        generation,
        int(configured),
        len(ssid_bytes),
        len(pass_bytes),
        0,
        ssid_bytes,
        pass_bytes,
        0,
    )
    crc = zlib.crc32(body[:-4]) & 0xFFFFFFFF
    return body[:-4] + struct.pack("<I", crc)


def decode_record(blob):
    if blob is None or len(blob) != RECORD.size:
        return None
    (
        magic,
        version,
        size,
        generation,
        configured,
        ssid_length,
        pass_length,
        reserved,
        ssid_buffer,
        pass_buffer,
        stored_crc,
    ) = RECORD.unpack(blob)
    if (
        magic != MAGIC
        or version != VERSION
        or size != RECORD.size
        or configured not in (0, 1)
        or reserved != 0
        or ssid_length > 32
        or pass_length > 64
        or (zlib.crc32(blob[:-4]) & 0xFFFFFFFF) != stored_crc
        or ssid_buffer[ssid_length] != 0
        or pass_buffer[pass_length] != 0
        or ssid_buffer.find(b"\0") != ssid_length
        or pass_buffer.find(b"\0") != pass_length
        or (not configured and (ssid_length != 0 or pass_length != 0))
        or (configured and ssid_length == 0)
    ):
        return None
    try:
        ssid = ssid_buffer[:ssid_length].decode("utf-8")
        password = pass_buffer[:pass_length].decode("utf-8")
    except UnicodeDecodeError:
        return None
    return generation, ssid, password


def generation_is_newer(candidate, reference):
    delta = (candidate - reference) & 0xFFFFFFFF
    return 0 < delta < 0x80000000


class WifiCredentialStoreModel:
    def __init__(self):
        self.slots = [None, None]
        self.active = None
        self.generation = 0
        self.ssid = ""
        self.password = ""

    def boot(self):
        valid = [decode_record(blob) for blob in self.slots]
        if valid[0] is None and valid[1] is None:
            self.active = None
            self.generation = 0
            self.ssid = ""
            self.password = ""
            return self.ssid, self.password
        selected = 0
        if valid[0] is None or (
            valid[1] is not None and generation_is_newer(valid[1][0], valid[0][0])
        ):
            selected = 1
        self.active = selected
        self.generation, self.ssid, self.password = valid[selected]
        return self.ssid, self.password

    def save(self, ssid, password, fault=None):
        target = 1 if self.active == 0 else 0
        generation = 1 if self.generation == 0xFFFFFFFF else self.generation + 1
        blob = encode_record(generation, ssid, password)
        if blob is None:
            return False
        if fault == "truncate":
            blob = blob[:-9]
        elif fault == "bitflip":
            blob = bytearray(blob)
            blob[25] ^= 0x40
            blob = bytes(blob)
        self.slots[target] = blob
        verified = decode_record(self.slots[target])
        if verified != (generation, ssid, password):
            return False
        self.active = target
        self.generation = generation
        self.ssid = ssid
        self.password = password
        return True


class WifiCredentialsPersistenceTests(unittest.TestCase):
    def test_ssid_and_password_round_trip_as_one_crc_protected_record(self):
        blob = encode_record(17, "Casa & Taller", "dog-collar-2026")
        self.assertEqual(decode_record(blob), (17, "Casa & Taller", "dog-collar-2026"))
        self.assertEqual(len(blob), 118)

    def test_record_supports_open_network_and_future_raw_64_byte_psk(self):
        self.assertEqual(decode_record(encode_record(1, "OpenHome", ""))[1:], ("OpenHome", ""))
        raw_psk = "a" * 64
        self.assertEqual(decode_record(encode_record(2, "RawPsk", raw_psk))[2], raw_psk)

    def test_corrupt_or_partial_record_is_rejected(self):
        blob = encode_record(8, "Home", "password")
        corrupt = bytearray(blob)
        corrupt[40] ^= 0x01
        self.assertIsNone(decode_record(bytes(corrupt)))
        self.assertIsNone(decode_record(blob[:-1]))
        self.assertIsNone(decode_record(blob + b"\0"))

    def test_interrupted_update_keeps_complete_previous_pair_in_ram_and_after_boot(self):
        store = WifiCredentialStoreModel()
        self.assertTrue(store.save("OldHome", "old-password"))
        previous_runtime = (store.ssid, store.password, store.active, store.generation)

        self.assertFalse(store.save("NewHome", "new-password", fault="truncate"))
        self.assertEqual(
            (store.ssid, store.password, store.active, store.generation),
            previous_runtime,
        )
        self.assertEqual(store.boot(), ("OldHome", "old-password"))

    def test_crc_failure_can_never_mix_ssid_and_password_generations(self):
        store = WifiCredentialStoreModel()
        self.assertTrue(store.save("Network-A", "password-A"))
        self.assertTrue(store.save("Network-B", "password-B"))
        self.assertFalse(store.save("Network-C", "password-C", fault="bitflip"))
        self.assertEqual(store.boot(), ("Network-B", "password-B"))

    def test_successful_updates_alternate_slots_and_wrap_generation_safely(self):
        store = WifiCredentialStoreModel()
        for index in range(1, 7):
            previous = store.active
            self.assertTrue(store.save(f"Home-{index}", f"password-{index}"))
            if previous is not None:
                self.assertNotEqual(store.active, previous)
        self.assertEqual(store.boot(), ("Home-6", "password-6"))
        self.assertTrue(generation_is_newer(1, 0xFFFFFFFF))
        self.assertFalse(generation_is_newer(0xFFFFFFFF, 1))

    def test_unconfigured_state_is_an_explicit_valid_future_clear_record(self):
        self.assertEqual(decode_record(encode_record(3, "", "")), (3, "", ""))
        self.assertIsNone(encode_record(4, "", "orphan-password"))

    def test_firmware_contract_uses_verified_ab_record_and_storage_error_response(self):
        wifi_cpp = (ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        wifi_h = (ROOT / "include/wifi/wifi_mgr.h").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        for required in (
            'WIFI_CREDS_RECORD_KEYS[2] = {"wifi_a", "wifi_b"}',
            "struct __attribute__((packed)) WifiCredentialsRecord",
            "static_assert(sizeof(WifiCredentialsRecord) == 118",
            "record.crc32 != wifi_creds_record_crc(record)",
            "wifi_creds_generation_is_newer",
            "prefs.putBytes(key, &record, sizeof(record)) != sizeof(record)",
            "memcmp(&record, &readback, sizeof(record)) != 0",
            "if (valid_a != valid_b)",
            "WIFI_CREDS_MIGRATED_KEY",
        ):
            self.assertIn(required, wifi_cpp)
        self.assertNotIn('putString("wifi_ssid"', wifi_cpp)
        self.assertNotIn('putString("wifi_pass"', wifi_cpp)
        self.assertIn("bool save_creds", wifi_h)
        handler_start = portal.index("void handle_wifi_save()")
        handler_end = portal.index("void begin()", handler_start)
        wifi_save_handler = portal[handler_start:handler_end]
        self.assertIn("if (!wifi_mgr::save_creds(ssid, pass))", wifi_save_handler)
        # All portal errors share the status/reason envelope; keep the Wi-Fi
        # storage failure consistent with the other write endpoints.
        self.assertIn(
            r'{\"status\":\"error\",\"reason\":\"storage\"}',
            wifi_save_handler,
        )
        self.assertLess(
            wifi_save_handler.index("if (!wifi_mgr::save_creds(ssid, pass))"),
            wifi_save_handler.index("wifi_mgr::start_sta_mode();"),
        )
        self.assertIn('wifiStorage["generation"]', portal)
        self.assertIn('wifiStorage["save_failures"]', portal)


if __name__ == "__main__":
    unittest.main()
