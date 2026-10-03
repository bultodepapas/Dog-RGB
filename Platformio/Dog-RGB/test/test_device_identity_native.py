"""Compile and execute the portable device identity core with a faultable backend."""

import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
from native_temp import native_temp_dir
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeviceIdentityNativeTests(unittest.TestCase):
    def test_cpp11_fault_matrix(self):
        compiler = shlex.split(os.environ.get("CXX", ""))
        if not compiler:
            found = shutil.which("g++") or shutil.which("clang++")
            self.assertIsNotNone(found, "g++ or clang++ is required for native identity tests")
            compiler = [found]

        with native_temp_dir(prefix="device-identity-native-") as directory:
            executable = Path(directory) / "device_identity_native"
            command = [
                *compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wshadow",
                "-pedantic",
                *shlex.split(os.environ.get("CXXFLAGS", "")),
                "-I",
                str(ROOT / "include"),
                str(ROOT / "test" / "device_identity_native.cpp"),
                str(ROOT / "src" / "track" / "device_identity.cpp"),
                "-o",
                str(executable),
            ]
            built = subprocess.run(
                command, capture_output=True, text=True, check=False, timeout=60
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False, timeout=30
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            match = re.fullmatch(r"PASS device_identity_native checks=([1-9][0-9]*)\n?", result.stdout)
            self.assertIsNotNone(match, result.stdout + result.stderr)
            self.assertGreater(int(match.group(1)), 300, result.stdout)
            self.assertEqual(result.stderr, "")
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
