"""Exercise the real native assembler, identity store and codec against Python."""

import hashlib
import os
from pathlib import Path
import queue
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools/cloud_phase0"))
import track_v3 as oracle  # noqa: E402


def expected_chunks():
    """Independent input/identity expectations for the C++ fixture batches."""
    first_id = uuid.UUID("00112233-4455-4677-8899-aabbccddeeff")
    second_id = uuid.UUID("fedcba98-7654-4321-8abc-def012345678")
    third_id = uuid.UUID("10325476-98ba-4cde-8001-23456789abcd")

    def moving(utc):
        return oracle.TrackPointV3(47110111, -740720123, utc, 1234, 10, 7)

    def stationary(utc):
        return oracle.TrackPointV3(47110112, -740720124, utc, 0, 7, 13)

    def chunk(device, points, sequence=0, first=0, quality=4, final=False):
        return oracle.TrackChunkV3(
            device, 1, sequence, first, oracle.TimeQuality(quality), final,
            tuple(points),
        )

    return {
        "golden": chunk(first_id, [moving(1750000000), stationary(1750000000)]),
        "next": chunk(first_id, [moving(1750000001)], sequence=1, first=2),
        "sha_retry": chunk(second_id, [moving(1750000200)]),
        "sha_next": chunk(second_id, [stationary(1750000201)], sequence=1, first=1),
        "gap": chunk(
            third_id, [oracle.TrackPointV3(0, 0, 0, 65535, 0, 32)],
            quality=0, final=True,
        ),
        "max96": chunk(first_id, [
            oracle.TrackPointV3(47100000 + i, -740700000 + i,
                                1750001000, 200, 8, 5)
            for i in range(96)
        ], quality=1),
        "final": chunk(second_id, [moving(1750000300)], final=True),
    }


class ChunkAssemblerNativeTests(unittest.TestCase):
    def test_native_state_machine_and_python_interoperability(self):
        compiler = shlex.split(os.environ.get("CXX", ""))
        if not compiler:
            found = shutil.which("g++") or shutil.which("clang++")
            self.assertIsNotNone(found, "a C++ compiler is required")
            compiler = [found]

        with tempfile.TemporaryDirectory(prefix="chunk-assembler-native-") as directory:
            executable = Path(directory) / "chunk_assembler_native"
            command = [
                *compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-Wshadow", "-pedantic",
                *shlex.split(os.environ.get("CXXFLAGS", "")),
                "-I", str(ROOT / "include"),
                str(ROOT / "test/chunk_assembler_native.cpp"),
                str(ROOT / "src/track/chunk_assembler.cpp"),
                str(ROOT / "src/track/device_identity.cpp"),
                str(ROOT / "src/track/track_v3.cpp"),
                "-o", str(executable),
            ]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            frames, checks = self.run_harness(executable)

        expected = expected_chunks()
        self.assertEqual(set(frames), set(expected))
        for label, chunk in expected.items():
            with self.subTest(frame=label):
                self.assertEqual(frames[label], oracle.encode_chunk(chunk))
                self.assertEqual(oracle.decode_chunk(frames[label]).chunk, chunk)
        print(f"PASS chunk_assembler_native checks={checks}; oracle frames={len(frames)}")

    def run_harness(self, executable):
        frames = {}
        checks = None
        hash_failures = 0
        # A file drains sanitizer stderr without risking a full stderr pipe.
        with tempfile.TemporaryFile(mode="w+") as errors:
            process = subprocess.Popen(
                [str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=errors, text=True, bufsize=1,
            )
            lines = queue.Queue()

            def pump():
                for line in process.stdout:
                    lines.put(line.rstrip("\n"))
                lines.put(None)

            reader = threading.Thread(target=pump, daemon=True)
            reader.start()
            deadline = time.monotonic() + 30
            try:
                while True:
                    remaining = deadline - time.monotonic()
                    self.assertGreater(remaining, 0, "native harness exceeded 30 seconds")
                    try:
                        line = lines.get(timeout=remaining)
                    except queue.Empty:
                        self.fail("native harness did not respond within 30 seconds")
                    if line is None:
                        break
                    self.assertLessEqual(len(line), 4096)
                    self.assertIsNone(checks, "unexpected output after PASS")
                    if line.startswith(("SHA ", "SHA_FAIL ")):
                        mode, payload_hex = line.split(" ", 1)
                        payload = bytes.fromhex(payload_hex)
                        self.assertTrue(16 <= len(payload) <= 96 * 16)
                        self.assertEqual(len(payload) % 16, 0)
                        if mode == "SHA_FAIL":
                            hash_failures += 1
                            response = "HASHFAIL"
                        else:
                            response = "HASH " + hashlib.sha256(payload).hexdigest()
                        process.stdin.write(response + "\n")
                        process.stdin.flush()
                    elif line.startswith("FRAME "):
                        _, label, frame_hex = line.split(" ", 2)
                        self.assertNotIn(label, frames, "duplicate fixture frame")
                        frames[label] = bytes.fromhex(frame_hex)
                    else:
                        matched = re.fullmatch(
                            r"PASS chunk_assembler_native checks=([1-9][0-9]*)", line
                        )
                        self.assertIsNotNone(matched, f"unexpected output: {line!r}")
                        checks = int(matched.group(1))
                self.assertEqual(process.wait(timeout=5), 0)
                errors.seek(0)
                self.assertEqual(errors.read(), "")
                self.assertIsNotNone(checks, "native harness did not report PASS")
                self.assertEqual(hash_failures, 1)
            except BaseException as exc:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=5)
                errors.seek(0)
                detail = errors.read()
                if detail:
                    exc.add_note(detail)
                raise
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                reader.join(timeout=1)
                process.stdin.close()
                process.stdout.close()
        return frames, checks


if __name__ == "__main__":
    unittest.main()
