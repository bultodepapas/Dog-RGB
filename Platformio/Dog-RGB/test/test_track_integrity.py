from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
TRACK_VERSION = 2
CHUNK_POINTS = 48
HEADER = struct.Struct("<BBHBI")
POINT = struct.Struct("<iiH")


def encode_chunk(points):
    first_min = points[0][2]
    header_without_crc = HEADER.pack(TRACK_VERSION, len(points), first_min, 0, 0)
    payload = b"".join(POINT.pack(*point) for point in points)
    crc = zlib.crc32(header_without_crc + payload) & 0xFFFFFFFF
    return HEADER.pack(TRACK_VERSION, len(points), first_min, 0, crc) + payload


def decode_chunk(blob):
    if len(blob) < HEADER.size + POINT.size:
        return None
    version, count, first_min, flags, stored_crc = HEADER.unpack_from(blob)
    if version != TRACK_VERSION or not 1 <= count <= CHUNK_POINTS:
        return None
    if flags != 0 or first_min >= 1440:
        return None
    if len(blob) != HEADER.size + count * POINT.size:
        return None
    crc_input = HEADER.pack(version, count, first_min, flags, 0) + blob[HEADER.size :]
    if zlib.crc32(crc_input) & 0xFFFFFFFF != stored_crc:
        return None

    points = [POINT.unpack_from(blob, HEADER.size + i * POINT.size) for i in range(count)]
    if points[0][2] != first_min:
        return None
    for lat_e7, lon_e7, minute in points:
        if not -900_000_000 <= lat_e7 <= 900_000_000:
            return None
        if not -1_800_000_000 <= lon_e7 <= 1_800_000_000:
            return None
        if not 0 <= minute < 1440:
            return None
    return points


class TrackIntegrityTests(unittest.TestCase):
    def test_crc32_uses_standard_ieee_vector(self):
        self.assertEqual(zlib.crc32(b"123456789") & 0xFFFFFFFF, 0xCBF43926)

    def test_valid_chunk_round_trip(self):
        points = [(45_500_000, -739_000_000, 720), (45_500_100, -738_999_900, 721)]
        self.assertEqual(decode_chunk(encode_chunk(points)), points)

    def test_payload_bit_flip_is_rejected(self):
        blob = bytearray(encode_chunk([(45_500_000, -739_000_000, 720)]))
        blob[-1] ^= 0x01
        self.assertIsNone(decode_chunk(bytes(blob)))

    def test_truncated_and_extended_blobs_are_rejected(self):
        blob = encode_chunk([(45_500_000, -739_000_000, 720)])
        self.assertIsNone(decode_chunk(blob[:-1]))
        self.assertIsNone(decode_chunk(blob + b"\x00"))

    def test_valid_crc_cannot_bypass_coordinate_validation(self):
        blob = encode_chunk([(900_000_001, -739_000_000, 720)])
        self.assertIsNone(decode_chunk(blob))

    def test_corrupt_chunk_does_not_hide_valid_neighbors(self):
        first = encode_chunk([(45_500_000, -739_000_000, 720)])
        corrupt = bytearray(encode_chunk([(45_500_100, -738_999_900, 721)]))
        corrupt[-2] ^= 0x40
        last = encode_chunk([(45_500_200, -738_999_800, 722)])

        visible = []
        for blob in (first, bytes(corrupt), last):
            decoded = decode_chunk(blob)
            if decoded is not None:
                visible.extend(decoded)
        self.assertEqual(
            visible,
            [(45_500_000, -739_000_000, 720), (45_500_200, -738_999_800, 722)],
        )

    def test_firmware_contract_uses_crc32_and_strict_decoder(self):
        gps_cpp = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        crc_h = (ROOT / "include/util/crc32.h").read_text(encoding="utf-8")

        self.assertIn("static const uint8_t TRACK_VER = 2;", gps_cpp)
        self.assertIn("uint32_t crc32;", gps_cpp)
        self.assertIn("bool track_load_chunk", gps_cpp)
        self.assertIn("if (len != expected_len)", gps_cpp)
        self.assertIn("stored_crc != util::crc32_ieee(buffer, len)", gps_cpp)
        self.assertIn("track_point_valid(points[i])", gps_cpp)
        self.assertIn("if (!track_load_chunk", gps_cpp)
        self.assertIn("continue;", gps_cpp)
        self.assertIn("meta.crc32 != track_meta_crc(meta)", gps_cpp)
        self.assertIn("track_current.meta_dirty = !track_save_current_meta(prefs)", gps_cpp)
        self.assertIn("if (track_current.meta_dirty)", gps_cpp)
        self.assertIn("0xEDB88320UL", crc_h)


if __name__ == "__main__":
    unittest.main()
