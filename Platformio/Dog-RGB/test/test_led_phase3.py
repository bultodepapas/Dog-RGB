import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LedPhase3Tests(unittest.TestCase):
    def test_native_layout_palette_and_transition_contracts(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for the pure LED contract test")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "led_phase3_characterization"
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-variable",
                "-pedantic",
                "-I",
                str(ROOT / "include"),
                str(ROOT / "test" / "led_phase3_characterization.cpp"),
                str(ROOT / "src" / "led" / "effect_registry.cpp"),
                str(ROOT / "src" / "led" / "led_color.cpp"),
                str(ROOT / "src" / "led" / "led_compositor.cpp"),
                str(ROOT / "src" / "led" / "led_layout.cpp"),
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
            self.assertEqual(
                run_result.stdout.strip(), "led_phase3_characterization: ok"
            )

    def test_hot_path_layers_stay_device_independent_and_allocation_free(self):
        forbidden = (
            "Arduino.h",
            "gps/",
            "wifi/",
            "geofence/",
            "config/runtime_config",
            "Preferences",
            "millis(",
            "new ",
            "malloc(",
            "std::vector",
        )
        layers = (
            "src/led/led_color.cpp",
            "src/led/led_compositor.cpp",
            "src/led/led_layout.cpp",
            "src/led/palette_registry.cpp",
        )
        for relative in layers:
            source = (ROOT / relative).read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, source, f"{relative} imports/uses {token}")

    def test_runtime_and_api_expose_phase3_capabilities_additively(self):
        portal = (ROOT / "src" / "web" / "portal_http.cpp").read_text(
            encoding="utf-8"
        )
        ui = (ROOT / "src" / "led" / "led_ui.cpp").read_text(encoding="utf-8")
        config_source = (ROOT / "src" / "config" / "runtime_config.cpp").read_text(
            encoding="utf-8"
        )

        for region in ("status", "body_left", "body_right", "body_all", "alert"):
            self.assertIn(f'"{region}"', (ROOT / "src/led/led_layout.cpp").read_text(encoding="utf-8"))
        self.assertIn('features["transitions"] = true', portal)
        self.assertIn('features["palettes"] = true', portal)
        self.assertIn('doc["palettes"]', portal)
        self.assertIn('doc["palette_count"]', portal)
        self.assertIn('out["dynamic"]', portal)
        self.assertIn('layout["bus_a_orientation"]', portal)
        self.assertIn('layout["bus_b_orientation"]', portal)
        self.assertIn('layout["mirror_default"]', portal)
        self.assertIn('limits["transition_default_ms"]', portal)
        self.assertIn('doc["alert"]', portal)
        self.assertIn('doc["transition_ms"]', portal)
        self.assertIn('doc["transition"]', portal)
        self.assertIn("compositor.begin_transition(led_frame", ui)
        self.assertIn("compositor.interrupt_for_alert()", ui)
        self.assertNotIn("show_transition_scale", ui)
        self.assertNotIn("show_next_base", ui)
        self.assertRegex(
            config_source, r"uint8_t\s+version\(\)\s*\{\s*return\s+6;\s*\}"
        )
        self.assertIn("CONFIG_RECORD_VERSION = 2", config_source)


if __name__ == "__main__":
    unittest.main()
