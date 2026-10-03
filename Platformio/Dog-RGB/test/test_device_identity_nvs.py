"""Compile the actual ESP-IDF adapter against a fault-injectable NVS API."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
NVS_HEADER = """
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t nvs_handle_t;
typedef int esp_err_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_TYPE_MISMATCH 0x1103
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
esp_err_t nvs_open_from_partition(const char *, const char *, nvs_open_mode_t,
                                nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
"""


class DeviceIdentityNvsTests(unittest.TestCase):
    def test_sdk_adapter_fault_contract(self):
        compiler = shlex.split(os.environ.get("CXX", ""))
        if not compiler:
            found = shutil.which("g++") or shutil.which("clang++")
            self.assertIsNotNone(found, "a C++ compiler is required")
            compiler = [found]
        with tempfile.TemporaryDirectory(prefix="identity-nvs-") as directory:
            build = Path(directory)
            (build / "nvs.h").write_text(NVS_HEADER, encoding="utf-8")
            executable = build / "identity_nvs"
            command = [
                *compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-Wshadow", "-pedantic",
                *shlex.split(os.environ.get("CXXFLAGS", "")),
                "-I", str(build), "-I", str(ROOT / "include"),
                str(ROOT / "src/track/device_identity_nvs.cpp"),
                str(ROOT / "test/device_identity_nvs_native.cpp"),
                "-o", str(executable),
            ]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertEqual(ran.stdout.strip(), "device_identity_nvs: ok")


if __name__ == "__main__":
    unittest.main()
