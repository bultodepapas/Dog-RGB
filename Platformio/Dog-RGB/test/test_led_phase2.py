import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LedPhase2Tests(unittest.TestCase):
    def test_native_characterization_and_policy_contracts(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for the pure LED contract test")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "led_phase2_characterization"
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(ROOT / "include"),
                str(ROOT / "test" / "led_phase2_characterization.cpp"),
                str(ROOT / "src" / "led" / "effect_registry.cpp"),
                str(ROOT / "src" / "led" / "led_color.cpp"),
                str(ROOT / "src" / "led" / "led_policy.cpp"),
                str(ROOT / "src" / "led" / "led_state.cpp"),
                str(ROOT / "src" / "led" / "palette_registry.cpp"),
                "-o",
                str(executable),
            ]
            compile_result = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )
            self.assertEqual(run_result.stdout.strip(), "led_phase2_characterization: ok")

    def test_core_led_layers_do_not_import_device_domains(self):
        forbidden = (
            "Arduino.h",
            "gps/",
            "wifi/",
            "geofence/",
            "config/runtime_config",
            "Preferences",
            "millis(",
        )
        for relative in (
            "src/led/effect_registry.cpp",
            "src/led/led_policy.cpp",
            "src/led/led_state.cpp",
        ):
            source = (ROOT / relative).read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, source, f"{relative} imports {token}")
            self.assertIsNone(
                re.search(r"(?<![A-Za-z0-9_])random\s*\(", source),
                f"{relative} calls Arduino's process-global random()",
            )

    def test_additive_api_and_legacy_persistence_contract(self):
        portal = (ROOT / "src" / "web" / "portal_http.cpp").read_text(
            encoding="utf-8"
        )
        pages = (ROOT / "src" / "web" / "pages.cpp").read_text(encoding="utf-8")
        config_source = (ROOT / "src" / "config" / "runtime_config.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('"/api/v1/led/state"', portal)
        self.assertIn('"/api/v1/led/capabilities"', portal)
        self.assertIn("effect_descriptor_count()", portal)
        self.assertIn("effect_id_valid(eff_a)", portal)
        self.assertIn("effect_id_valid(single_eff)", portal)
        self.assertNotIn("effect_id_valid(static_cast<uint8_t>", portal)
        self.assertIn("/api/v1/led/capabilities", pages)
        self.assertNotIn("const EFFECTS", pages)
        self.assertRegex(
            config_source, r"uint8_t\s+version\(\)\s*\{\s*return\s+6;\s*\}"
        )
        self.assertIn("CONFIG_RECORD_VERSION = 2", config_source)


if __name__ == "__main__":
    unittest.main()
