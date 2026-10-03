"""Cross-check the native Track v3 codec against the frozen Python oracle."""

import hashlib
import json
import os
from pathlib import Path
import queue
import shlex
import shutil
import struct
import subprocess
import tempfile
import threading
import unittest
import uuid
import zlib
import sys


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]
REFERENCE_DIR = REPO_ROOT / "tools" / "cloud_phase0"
sys.path.insert(0, str(REFERENCE_DIR))
import track_v3 as oracle  # noqa: E402
from reference_fixtures import reference_fixtures  # noqa: E402


NO_SHA = object()
ANY_SHA = object()
UINT32_MAX = 0xFFFFFFFF
HEADER = oracle.CHUNK_HEADER_STRUCT
HEADER_SIZE = oracle.CHUNK_HEADER_SIZE
POINT = oracle.POINT_STRUCT
POINT_SIZE = oracle.POINT_SIZE
DEVICE_ID = uuid.UUID("00112233-4455-4677-8899-aabbccddeeff")
SECOND_DEVICE_ID = uuid.UUID("fedcba98-7654-4321-8abc-def012345678")


def sample_points(count, quality):
    """Build semantically valid points while exercising every defined flag."""
    points = []
    is_legacy = quality == oracle.TimeQuality.LEGACY_MINUTE
    has_time = quality != oracle.TimeQuality.UNKNOWN
    for index in range(count):
        utc_s = 1_750_000_000 + index if has_time else 0
        flags = oracle.PointFlag.LEGACY_V2 if is_legacy else oracle.PointFlag(0)
        if has_time:
            flags |= oracle.PointFlag.TIME_TRUSTED
        is_gap = index % 13 == 12
        if is_gap:
            flags |= oracle.PointFlag.GAP
            latitude = 0
            longitude = 0
            speed = oracle.SPEED_UNAVAILABLE
        else:
            flags |= oracle.PointFlag.FIX_VALID
            latitude = 900_000_000 - (index % 1000)
            longitude = -1_800_000_000 + (index % 1000)
            speed = (index * 997) % oracle.SPEED_UNAVAILABLE
            if index % 3 == 0:
                flags |= oracle.PointFlag.MOVEMENT_EVIDENCE
            elif index % 3 == 1:
                flags |= oracle.PointFlag.STATIONARY_HEARTBEAT
        if index % 7 == 0:
            flags |= oracle.PointFlag.LOW_QUALITY
        points.append(
            oracle.TrackPointV3(
                lat_e7=latitude,
                lon_e7=longitude,
                utc_s=utc_s,
                speed_cmps=speed,
                satellites=index % 256,
                flags=int(flags),
            )
        )
    return tuple(points)


def sample_chunk(count, quality, *, device_id=None, first_point_sequence=None):
    quality = oracle.TimeQuality(quality)
    return oracle.TrackChunkV3(
        device_id=device_id or (DEVICE_ID if int(quality) % 2 == 0 else SECOND_DEVICE_ID),
        boot_sequence=0 if quality == oracle.TimeQuality.LEGACY_MINUTE else 0xE1020304,
        chunk_sequence=UINT32_MAX - (count % 3),
        first_point_sequence=(
            UINT32_MAX - max(0, count - 1)
            if first_point_sequence is None
            else first_point_sequence
        ),
        time_quality=quality,
        final_for_recording=bool(count % 2),
        points=sample_points(count, quality),
    )


def payload_for(points):
    return b"".join(oracle.encode_point(point) for point in points)


def encode_command(
    chunk,
    *,
    out_cap=None,
    staging_cap=None,
    sha_mode=0,
):
    if out_cap is None:
        out_cap = HEADER_SIZE + len(chunk.points) * POINT_SIZE
    if staging_cap is None:
        staging_cap = HEADER_SIZE + len(chunk.points) * POINT_SIZE
    fields = [
        "E",
        str(out_cap),
        str(staging_cap),
        str(sha_mode),
        chunk.device_id.hex,
        str(chunk.boot_sequence),
        str(chunk.chunk_sequence),
        str(chunk.first_point_sequence),
        str(int(chunk.time_quality)),
        str(int(chunk.final_for_recording)),
        str(len(chunk.points)),
    ]
    for point in chunk.points:
        fields.extend(
            str(value)
            for value in (
                point.lat_e7,
                point.lon_e7,
                point.utc_s,
                point.speed_cmps,
                point.satellites,
                point.flags,
            )
        )
    return " ".join(fields)


def decode_command(blob, *, points_cap, sha_mode=0):
    return f"D {points_cap} {sha_mode} {blob.hex()}"


def mutate_chunk(blob, *, payload=None, changes=None, update_crc=True, update_sha=True,
                 update_header_crc=True):
    values = list(HEADER.unpack_from(blob))
    if payload is None:
        payload = blob[HEADER_SIZE:]
    if changes:
        for field, value in changes.items():
            values[field] = value
    if update_crc:
        values[14] = zlib.crc32(payload) & UINT32_MAX
    if update_sha:
        values[15] = hashlib.sha256(payload).digest()
    if update_header_crc:
        values[16] = 0
        values[16] = zlib.crc32(HEADER.pack(*values)) & UINT32_MAX
    return HEADER.pack(*values) + payload


def repack_points(blob, points):
    payload = b"".join(POINT.pack(*point) for point in points)
    values = list(HEADER.unpack_from(blob))
    values[8] = len(points)
    values[11] = min((point[2] for point in points if point[2]), default=0)
    values[12] = max((point[2] for point in points if point[2]), default=0)
    values[13] = len(payload)
    return mutate_chunk(blob, payload=payload, changes={8: len(points), 11: values[11],
                                                         12: values[12], 13: len(payload)})


class NativeProcess:
    """Drive the C++ harness and answer SHA requests with real hashlib output."""

    def __init__(self, executable):
        self.process = subprocess.Popen(
            [str(executable)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._lines = queue.Queue()

        def read_lines():
            assert self.process.stdout is not None
            for line in self.process.stdout:
                self._lines.put(line)
            self._lines.put(None)

        self._reader = threading.Thread(target=read_lines, daemon=True)
        self._reader.start()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close(terminate=exc_type is not None)

    def _readline(self):
        try:
            line = self._lines.get(timeout=10)
        except queue.Empty as exc:
            self.process.terminate()
            self.process.wait(timeout=5)
            raise AssertionError("native codec protocol timed out after 10 seconds") from exc
        if line is None:
            raise AssertionError("native harness closed its output pipe")
        return line.rstrip("\n")

    def call(self, command, *, expected_sha=NO_SHA, fail_sha=False):
        assert self.process.stdin is not None
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        sha_inputs = []
        while True:
            line = self._readline()
            if line.startswith("SHA "):
                data = bytes.fromhex(line[4:])
                sha_inputs.append(data)
                if fail_sha:
                    self.process.stdin.write("HASHFAIL\n")
                else:
                    digest = hashlib.sha256(data).hexdigest()
                    self.process.stdin.write(f"HASH {digest}\n")
                self.process.stdin.flush()
                continue
            if not line.startswith("RESULT "):
                raise AssertionError(f"unexpected native protocol line: {line!r}")
            if expected_sha is NO_SHA:
                if sha_inputs:
                    raise AssertionError(f"unexpected SHA callback input: {sha_inputs!r}")
            elif expected_sha is not ANY_SHA:
                if sha_inputs != [expected_sha]:
                    raise AssertionError(
                        "SHA callback did not receive the exact expected payload: "
                        f"{sha_inputs!r} != {[expected_sha]!r}"
                    )
            elif len(sha_inputs) > 1:
                raise AssertionError("native codec requested SHA more than once")
            return line.split()

    def close(self, *, terminate=False):
        response = None
        if self.process.poll() is None:
            if terminate:
                self.process.terminate()
            else:
                assert self.process.stdin is not None
                self.process.stdin.write("QUIT\n")
                self.process.stdin.flush()
                response = self._readline()
            return_code = self.process.wait(timeout=5)
        else:
            return_code = self.process.returncode
        stderr = self.process.stderr.read() if self.process.stderr is not None else ""
        for pipe in (self.process.stdin, self.process.stdout, self.process.stderr):
            if pipe is not None:
                pipe.close()
        self._reader.join(timeout=1)
        if not terminate and (response != "BYE" or return_code != 0):
            raise AssertionError(
                f"native harness shutdown failed: {response!r}, {return_code}, {stderr}"
            )


class TrackV3NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CXX", ""))
        if not compiler:
            found = shutil.which("g++") or shutil.which("clang++")
            if found is None:
                raise AssertionError("g++ or clang++ is required for the native codec test")
            compiler = [found]
        cls._build_dir = tempfile.TemporaryDirectory(prefix="track-v3-native-")
        cls.addClassCleanup(cls._build_dir.cleanup)
        cls.executable = Path(cls._build_dir.name) / "track_v3_native"
        extra_flags = shlex.split(os.environ.get("CXXFLAGS", ""))
        command = [
            *compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wshadow",
            "-pedantic",
            *extra_flags,
            "-I",
            str(ROOT / "include"),
            str(ROOT / "test" / "track_v3_native.cpp"),
            str(ROOT / "src" / "track" / "track_v3.cpp"),
            "-o",
            str(cls.executable),
        ]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)

    def native(self):
        return NativeProcess(self.executable)

    @staticmethod
    def assert_unchanged_result(result, operation, status):
        if operation == "E":
            self_status = result[2]
            unchanged = result[4:6]
            if self_status != status or unchanged != ["1", "1"]:
                raise AssertionError(f"expected {status} with intact encode outputs: {result}")
        elif operation == "D":
            if result[2] != status or result[3] != "1":
                raise AssertionError(f"expected {status} with intact decode outputs: {result}")
        elif operation in ("P", "Q"):
            if result[2] != status or result[3] != "1":
                raise AssertionError(f"expected {status} with intact point output: {result}")
        else:
            raise AssertionError(f"unknown native operation {operation}")

    def test_python_cpp_encode_decode_round_trip_for_all_sizes_and_qualities(self):
        with self.native() as native:
            for quality in oracle.TimeQuality:
                for count in range(1, oracle.MAX_POINTS_PER_CHUNK + 1):
                    with self.subTest(quality=quality.name, point_count=count):
                        chunk = sample_chunk(count, quality)
                        expected = oracle.encode_chunk(chunk)
                        decoded_reference = oracle.decode_chunk(expected).chunk
                        self.assertEqual(decoded_reference, chunk)
                        payload = payload_for(chunk.points)

                        encoded = native.call(
                            encode_command(chunk), expected_sha=payload
                        )
                        self.assertEqual(encoded[2], "ok", encoded)
                        self.assertEqual(int(encoded[3]), len(expected))
                        self.assertEqual(encoded[4:6], ["0", "0"])
                        self.assertEqual(bytes.fromhex(encoded[6]), expected)

                        decoded = native.call(
                            decode_command(
                                expected, points_cap=len(chunk.points)
                            ),
                            expected_sha=payload,
                        )
                        self.assertEqual(decoded[2:4], ["ok", "0"], decoded)
                        self.assertEqual(decoded[4], chunk.device_id.hex)
                        self.assertEqual(int(decoded[5]), chunk.boot_sequence)
                        self.assertEqual(int(decoded[6]), chunk.chunk_sequence)
                        self.assertEqual(
                            int(decoded[7]), chunk.first_point_sequence
                        )
                        self.assertEqual(int(decoded[8]), int(chunk.time_quality))
                        self.assertEqual(
                            int(decoded[9]), int(chunk.final_for_recording)
                        )
                        self.assertEqual(
                            int(decoded[10]), zlib.crc32(payload) & UINT32_MAX
                        )
                        self.assertEqual(
                            bytes.fromhex(decoded[11]), hashlib.sha256(payload).digest()
                        )
                        self.assertEqual(int(decoded[12]), count)
                        self.assertEqual(decoded[13], "1")
                        observed_points = [
                            tuple(map(int, decoded[offset : offset + 6]))
                            for offset in range(14, len(decoded), 6)
                        ]
                        expected_points = [
                            (
                                point.lat_e7,
                                point.lon_e7,
                                point.utc_s,
                                point.speed_cmps,
                                point.satellites,
                                point.flags,
                            )
                            for point in chunk.points
                        ]
                        self.assertEqual(observed_points, expected_points)

    def test_four_canonical_python_reference_fixtures_match_cpp(self):
        manifest = json.loads(
            (REFERENCE_DIR / "fixtures" / "reference_manifest.json").read_text(
                encoding="utf-8"
            )
        )
        expected_by_id = {
            item["fixture_id"]: item
            for item in manifest["fixtures"]
        }
        fixtures = reference_fixtures()
        self.assertEqual(len(fixtures), 4)
        self.assertEqual(set(expected_by_id), {item.fixture_id for item in fixtures})
        with self.native() as native:
            for fixture in fixtures:
                with self.subTest(fixture=fixture.fixture_id):
                    expected = oracle.encode_chunk(fixture.chunk)
                    manifest_item = expected_by_id[fixture.fixture_id]
                    self.assertEqual(
                        hashlib.sha256(expected).hexdigest(),
                        manifest_item["encoded_chunk_sha256"],
                    )
                    self.assertEqual(len(expected), manifest_item["encoded_chunk_bytes"])
                    payload = payload_for(fixture.chunk.points)
                    encoded = native.call(
                        encode_command(fixture.chunk), expected_sha=payload
                    )
                    self.assertEqual(encoded[2], "ok", encoded)
                    self.assertEqual(bytes.fromhex(encoded[6]), expected)
                    decoded = native.call(
                        decode_command(
                            expected, points_cap=len(fixture.chunk.points)
                        ),
                        expected_sha=payload,
                    )
                    self.assertEqual(decoded[2:4], ["ok", "0"], decoded)
                    self.assertEqual(decoded[4], fixture.chunk.device_id.hex)
                    self.assertEqual(int(decoded[12]), len(fixture.chunk.points))

    def test_point_codec_bytes_and_validation(self):
        point = sample_points(5, oracle.TimeQuality.SERVER_ANCHORED)[0]
        with self.native() as native:
            encoded = native.call(
                "P 16 " + " ".join(
                    str(value)
                    for value in (
                        point.lat_e7,
                        point.lon_e7,
                        point.utc_s,
                        point.speed_cmps,
                        point.satellites,
                        point.flags,
                    )
                )
            )
            expected_wire = oracle.encode_point(point)
            self.assertEqual(encoded[2:4], ["ok", "0"])
            self.assertEqual(bytes.fromhex(encoded[4]), expected_wire)

            decoded = native.call("Q " + expected_wire.hex())
            self.assertEqual(decoded[2:4], ["ok", "0"])
            self.assertEqual(
                tuple(map(int, decoded[4:10])),
                (
                    point.lat_e7,
                    point.lon_e7,
                    point.utc_s,
                    point.speed_cmps,
                    point.satellites,
                    point.flags,
                ),
            )

            short_output = native.call(
                "P 15 " + " ".join(
                    str(value)
                    for value in (
                        point.lat_e7,
                        point.lon_e7,
                        point.utc_s,
                        point.speed_cmps,
                        point.satellites,
                        point.flags,
                    )
                )
            )
            self.assert_unchanged_result(short_output, "P", "buffer_too_small")
            for malformed_wire in (expected_wire[:-1], expected_wire + b"\x00"):
                result = native.call("Q " + malformed_wire.hex())
                self.assert_unchanged_result(result, "Q", "invalid_length")

            bad_points = (
                oracle.TrackPointV3(900_000_001, 0, 1_750_000_000, 10, 6, 0x05),
                oracle.TrackPointV3(1, 0, 1_750_000_000, 10, 6, 0x04),
                oracle.TrackPointV3(
                    45_000_000,
                    -700_000_000,
                    1_750_000_000,
                    10,
                    6,
                    0x07 | 0x08,
                ),
                oracle.TrackPointV3(0, 0, 0, 0, 0, int(oracle.PointFlag.GAP)),
                oracle.TrackPointV3(0, 0, 1_750_000_000, 0xFFFF, 6, 0x00),
                oracle.TrackPointV3(
                    0,
                    0,
                    1_750_000_000,
                    0xFFFF,
                    6,
                    int(oracle.PointFlag.GAP | oracle.PointFlag.MOVEMENT_EVIDENCE),
                ),
                oracle.TrackPointV3(0, 0, 0, 0xFFFF, 6, 0x80),
            )
            for invalid in bad_points:
                with self.subTest(point=invalid):
                    with self.assertRaises(ValueError):
                        oracle.encode_point(invalid)
                    values = (
                        invalid.lat_e7,
                        invalid.lon_e7,
                        invalid.utc_s,
                        invalid.speed_cmps,
                        invalid.satellites,
                        invalid.flags,
                    )
                    encoded_invalid = native.call(
                        "P 16 " + " ".join(str(value) for value in values)
                    )
                    self.assert_unchanged_result(
                        encoded_invalid, "P", "invalid_point"
                    )
                    raw_invalid = POINT.pack(*values)
                    decoded_invalid = native.call("Q " + raw_invalid.hex())
                    self.assert_unchanged_result(
                        decoded_invalid, "Q", "invalid_point"
                    )

    def test_numeric_and_sequence_boundaries(self):
        boundary_points = (
            oracle.TrackPointV3(
                -900_000_000,
                -1_800_000_000,
                0,
                0,
                0,
                int(oracle.PointFlag.FIX_VALID),
            ),
            oracle.TrackPointV3(
                900_000_000,
                1_800_000_000,
                0,
                0xFFFF,
                0xFF,
                int(oracle.PointFlag.FIX_VALID),
            ),
        )
        unknown_chunk = oracle.TrackChunkV3(
            device_id=DEVICE_ID,
            boot_sequence=1,
            chunk_sequence=UINT32_MAX,
            first_point_sequence=UINT32_MAX - 1,
            time_quality=oracle.TimeQuality.UNKNOWN,
            final_for_recording=True,
            points=boundary_points,
        )
        max_time_chunk = oracle.TrackChunkV3(
            device_id=SECOND_DEVICE_ID,
            boot_sequence=UINT32_MAX,
            chunk_sequence=UINT32_MAX,
            first_point_sequence=UINT32_MAX,
            time_quality=oracle.TimeQuality.GNSS_TRUSTED,
            final_for_recording=False,
            points=(
                oracle.TrackPointV3(
                    -900_000_000,
                    1_800_000_000,
                    UINT32_MAX,
                    0xFFFF,
                    0xFF,
                    int(oracle.PointFlag.FIX_VALID | oracle.PointFlag.TIME_TRUSTED),
                ),
            ),
        )
        with self.native() as native:
            for chunk in (unknown_chunk, max_time_chunk):
                expected = oracle.encode_chunk(chunk)
                payload = payload_for(chunk.points)
                encoded = native.call(
                    encode_command(chunk), expected_sha=payload
                )
                self.assertEqual(encoded[2], "ok", encoded)
                self.assertEqual(bytes.fromhex(encoded[6]), expected)
                decoded = native.call(
                    decode_command(expected, points_cap=len(chunk.points)),
                    expected_sha=payload,
                )
                self.assertEqual(decoded[2:4], ["ok", "0"], decoded)
                self.assertEqual(int(decoded[7]), chunk.first_point_sequence)

            overflow_chunk = oracle.TrackChunkV3(
                device_id=DEVICE_ID,
                boot_sequence=1,
                chunk_sequence=1,
                first_point_sequence=UINT32_MAX,
                time_quality=oracle.TimeQuality.UNKNOWN,
                final_for_recording=False,
                points=boundary_points,
            )
            with self.assertRaises(ValueError):
                oracle.encode_chunk(overflow_chunk)
            overflow = native.call(encode_command(overflow_chunk))
            self.assert_unchanged_result(
                overflow, "E", "point_sequence_overflow"
            )

    def test_encode_and_decode_aliases_are_rejected_atomically(self):
        blob = oracle.encode_chunk(
            sample_chunk(2, oracle.TimeQuality.GNSS_TRUSTED)
        )
        with self.native() as native:
            result = native.call("A " + blob.hex())
            self.assertEqual(result, ["RESULT", "A", "ok", "11"], result)

    def test_limits_buffers_and_hash_callback_failures_do_not_publish_partial_output(self):
        chunk = sample_chunk(2, oracle.TimeQuality.SERVER_ANCHORED)
        expected = oracle.encode_chunk(chunk)
        payload = payload_for(chunk.points)
        required = len(expected)
        with self.native() as native:
            with self.assertRaises(ValueError):
                oracle.encode_chunk(
                    sample_chunk(0, oracle.TimeQuality.SERVER_ANCHORED)
                )
            with self.assertRaises(ValueError):
                oracle.encode_chunk(
                    sample_chunk(97, oracle.TimeQuality.SERVER_ANCHORED)
                )
            for invalid_count in (0, 97):
                invalid_chunk = sample_chunk(
                    invalid_count, oracle.TimeQuality.SERVER_ANCHORED
                )
                result = native.call(encode_command(invalid_chunk))
                self.assert_unchanged_result(result, "E", "invalid_chunk")

            too_small_out = native.call(
                encode_command(chunk, out_cap=required - 1),
                expected_sha=ANY_SHA,
            )
            self.assert_unchanged_result(
                too_small_out, "E", "buffer_too_small"
            )
            too_small_staging = native.call(
                encode_command(chunk, staging_cap=required - 1),
                expected_sha=ANY_SHA,
            )
            self.assert_unchanged_result(
                too_small_staging, "E", "buffer_too_small"
            )

            no_sha = native.call(encode_command(chunk, sha_mode=2))
            self.assert_unchanged_result(no_sha, "E", "sha_unavailable")
            failed_sha = native.call(
                encode_command(chunk, sha_mode=1),
                expected_sha=payload,
                fail_sha=True,
            )
            self.assert_unchanged_result(failed_sha, "E", "sha_failure")

            short_decode = native.call(
                decode_command(expected, points_cap=len(chunk.points) - 1),
                expected_sha=ANY_SHA,
            )
            self.assert_unchanged_result(
                short_decode, "D", "buffer_too_small"
            )
            no_decode_sha = native.call(
                decode_command(
                    expected, points_cap=len(chunk.points), sha_mode=2
                )
            )
            self.assert_unchanged_result(
                no_decode_sha, "D", "sha_unavailable"
            )
            failed_decode_sha = native.call(
                decode_command(
                    expected, points_cap=len(chunk.points), sha_mode=1
                ),
                expected_sha=payload,
                fail_sha=True,
            )
            self.assert_unchanged_result(
                failed_decode_sha, "D", "sha_failure"
            )

            for malformed in (expected[:-1], expected + b"\x00"):
                with self.assertRaises(ValueError):
                    oracle.decode_chunk(malformed)
                result = native.call(
                    decode_command(malformed, points_cap=len(chunk.points))
                )
                self.assert_unchanged_result(result, "D", "invalid_length")

    def test_decoder_rejects_checksums_structure_semantics_and_sequence_overflow(self):
        base_chunk = sample_chunk(4, oracle.TimeQuality.SERVER_ANCHORED)
        base_blob = oracle.encode_chunk(base_chunk)
        base_payload = base_blob[HEADER_SIZE:]
        points = [POINT.unpack_from(base_payload, offset)
                  for offset in range(0, len(base_payload), POINT_SIZE)]

        def point_case(index, **changes):
            values = list(points[index])
            names = ("lat_e7", "lon_e7", "utc_s", "speed_cmps", "satellites", "flags")
            for name, value in changes.items():
                values[names.index(name)] = value
            changed = list(points)
            changed[index] = tuple(values)
            return repack_points(base_blob, changed)

        bad_points = sample_points(4, oracle.TimeQuality.SERVER_ANCHORED)
        first = bad_points[0]
        first_values = (
            first.lat_e7,
            first.lon_e7,
            first.utc_s,
            first.speed_cmps,
            first.satellites,
            first.flags,
        )
        no_fix_flags = first.flags & ~int(
            oracle.PointFlag.FIX_VALID
            | oracle.PointFlag.MOVEMENT_EVIDENCE
            | oracle.PointFlag.STATIONARY_HEARTBEAT
        )
        gap_moving_flags = int(
            oracle.PointFlag.GAP
            | oracle.PointFlag.MOVEMENT_EVIDENCE
            | oracle.PointFlag.TIME_TRUSTED
        )

        malformed = []
        header_crc_bad = bytearray(base_blob)
        header_crc_bad[40] ^= 0x01
        malformed.append(("header_crc", bytes(header_crc_bad), "header_crc_mismatch", True))
        payload_crc_bad = bytearray(base_blob)
        payload_crc_bad[-1] ^= 0x01
        malformed.append(("payload_crc", bytes(payload_crc_bad), "payload_crc_mismatch", True))
        sha_bad = mutate_chunk(
            base_blob,
            changes={15: bytes([base_blob[56] ^ 0x80]) + base_blob[57:88]},
            update_crc=False,
            update_sha=False,
        )
        malformed.append(("payload_sha", sha_bad, "payload_sha_mismatch", True))
        wrong_magic = bytearray(base_blob)
        wrong_magic[0] ^= 0x20
        malformed.append(("magic", bytes(wrong_magic), "bad_magic", True))
        malformed.append(("schema", mutate_chunk(base_blob, changes={1: 4}), "unsupported_version", True))
        malformed.append(("header_version", mutate_chunk(base_blob, changes={2: 2}), "unsupported_version", True))
        malformed.append(("chunk_flag", mutate_chunk(base_blob, changes={3: 2}), "reserved_bits", True))
        malformed.append(("reserved_byte", mutate_chunk(base_blob, changes={10: 1}), "reserved_bits", True))
        malformed.append(("unknown_quality", mutate_chunk(base_blob, changes={9: 6}), "unknown_time_quality", True))
        malformed.append(("nil_uuid", mutate_chunk(base_blob, changes={4: b"\x00" * 16}), "invalid_chunk", True))
        malformed.append(("time_bounds", mutate_chunk(base_blob, changes={11: 1}), "time_bounds_mismatch", True))
        malformed.append(("latitude_range", point_case(0, lat_e7=900_000_001), "invalid_point", True))
        malformed.append(("unknown_point_flag", point_case(0, flags=first.flags | 0x80), "invalid_point", True))
        malformed.append(("mutually_exclusive_flags", point_case(0, flags=first.flags | int(oracle.PointFlag.STATIONARY_HEARTBEAT)), "invalid_point", True))
        malformed.append(("trusted_time_mismatch", point_case(0, flags=first.flags & ~int(oracle.PointFlag.TIME_TRUSTED)), "invalid_point", True))
        malformed.append(("coordinates_without_fix", point_case(0, flags=no_fix_flags), "invalid_point", True))
        malformed.append(("gap_with_fix", point_case(0, flags=first.flags | int(oracle.PointFlag.GAP)), "invalid_point", True))
        malformed.append(("gap_with_motion", point_case(0, lat_e7=0, lon_e7=0, speed_cmps=0xFFFF, flags=gap_moving_flags), "invalid_point", True))
        malformed.append(("speed_without_fix", point_case(0, lat_e7=0, lon_e7=0, speed_cmps=1, flags=no_fix_flags), "invalid_point", True))
        malformed.append(("zero_time_in_known_quality", point_case(0, utc_s=0, flags=first.flags & ~int(oracle.PointFlag.TIME_TRUSTED)), "invalid_chunk", True))
        nonmonotonic = list(points)
        nonmonotonic[0] = (*nonmonotonic[0][:2], nonmonotonic[1][2], *nonmonotonic[0][3:])
        nonmonotonic[1] = (*nonmonotonic[1][:2], nonmonotonic[0][2] - 1, *nonmonotonic[1][3:])
        malformed.append(("nonmonotonic_time", repack_points(base_blob, nonmonotonic), "invalid_chunk", True))
        malformed.append(("native_boot_zero", mutate_chunk(base_blob, changes={5: 0}), "invalid_chunk", True))
        malformed.append(("legacy_on_native", point_case(0, flags=first.flags | int(oracle.PointFlag.LEGACY_V2)), "invalid_chunk", True))

        legacy_blob = oracle.encode_chunk(
            sample_chunk(2, oracle.TimeQuality.LEGACY_MINUTE)
        )
        legacy_points = [POINT.unpack_from(legacy_blob, offset)
                         for offset in range(HEADER_SIZE, len(legacy_blob), POINT_SIZE)]
        legacy_points[0] = (
            *legacy_points[0][:5],
            legacy_points[0][5] & ~int(oracle.PointFlag.LEGACY_V2),
        )
        malformed.append(("legacy_missing_flag", repack_points(legacy_blob, legacy_points), "invalid_chunk", True))

        sequence_overflow = mutate_chunk(
            base_blob, changes={7: UINT32_MAX}
        )
        malformed.append(("sequence_overflow", sequence_overflow, "point_sequence_overflow", False))
        zero_count = mutate_chunk(base_blob, changes={8: 0, 13: 0}, payload=b"")
        malformed.append(("zero_count", zero_count, "invalid_length", True))

        with self.native() as native:
            for name, blob, expected_status, python_rejects in malformed:
                with self.subTest(corruption=name):
                    if python_rejects:
                        with self.assertRaises(ValueError):
                            oracle.decode_chunk(blob)
                    else:
                        # The frozen oracle omits this decoder range check; native
                        # must still reject a wrapping point-sequence interval.
                        oracle.decode_chunk(blob)
                    result = native.call(
                        decode_command(blob, points_cap=oracle.MAX_POINTS_PER_CHUNK),
                        expected_sha=ANY_SHA,
                    )
                    self.assert_unchanged_result(result, "D", expected_status)


if __name__ == "__main__":
    unittest.main()
