import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LedPhase4Tests(unittest.TestCase):
    def test_native_scene_catalog_player_and_store_contracts(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for the pure scene test")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "led_phase4_characterization"
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(ROOT / "include"),
                str(ROOT / "test" / "led_phase4_characterization.cpp"),
                str(ROOT / "src" / "led" / "effect_registry.cpp"),
                str(ROOT / "src" / "led" / "led_color.cpp"),
                str(ROOT / "src" / "led" / "palette_registry.cpp"),
                str(ROOT / "src" / "led" / "scene.cpp"),
                str(ROOT / "src" / "led" / "scene_catalog.cpp"),
                str(ROOT / "src" / "led" / "scene_player.cpp"),
                str(ROOT / "src" / "storage" / "scene_store.cpp"),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(
                compiled.returncode, 0, compiled.stdout + compiled.stderr
            )
            run = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(
                run.stdout.strip(), "led_phase4_characterization: ok"
            )

    def test_native_scene_json_allowlist_and_round_trip(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for the scene JSON test")
        arduino_json = ROOT / ".pio" / "libdeps" / "seeed_xiao_esp32s3" / "ArduinoJson" / "src"
        self.assertTrue(arduino_json.exists(), "run the firmware build to install ArduinoJson")
        # Windows Defender can briefly retain the just-run executable. Test
        # assertions still run; cleanup is best-effort for this temp artifact.
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            executable = Path(directory) / "scene_json_characterization"
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(ROOT / "include"),
                "-I",
                str(arduino_json),
                str(ROOT / "test" / "scene_json_characterization.cpp"),
                str(ROOT / "src" / "led" / "effect_registry.cpp"),
                str(ROOT / "src" / "led" / "led_color.cpp"),
                str(ROOT / "src" / "led" / "palette_registry.cpp"),
                str(ROOT / "src" / "led" / "scene.cpp"),
                str(ROOT / "src" / "led" / "scene_catalog.cpp"),
                str(ROOT / "src" / "web" / "scene_json.cpp"),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(
                compiled.returncode, 0, compiled.stdout + compiled.stderr
            )
            run = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(
                run.stdout.strip(), "scene_json_characterization: ok"
            )

    def test_scene_core_stays_device_independent_and_allocation_free(self):
        forbidden = (
            "Arduino.h",
            "Preferences",
            "ArduinoJson",
            "String ",
            "new ",
            "malloc(",
            "std::vector",
            "std::string",
            "millis(",
        )
        layers = (
            "src/led/scene.cpp",
            "src/led/scene_catalog.cpp",
            "src/led/scene_player.cpp",
            "src/storage/scene_store.cpp",
        )
        for relative in layers:
            source = (ROOT / relative).read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, source, f"{relative} imports/uses {token}")

    def test_runtime_api_is_additive_bounded_and_uses_raw_body_streaming(self):
        portal = (ROOT / "src" / "web" / "portal_http.cpp").read_text(
            encoding="utf-8"
        )
        ui = (ROOT / "src" / "led" / "led_ui.cpp").read_text(encoding="utf-8")
        config_h = (ROOT / "include" / "config.h").read_text(encoding="utf-8")
        runtime_config = (ROOT / "src" / "config" / "runtime_config.cpp").read_text(
            encoding="utf-8"
        )
        nvs = (ROOT / "src" / "storage" / "nvs_store.cpp").read_text(
            encoding="utf-8"
        )

        routes = (
            ("/api/v1/led/scenes", "HTTP_GET"),
            ("/api/v1/led/scenes/apply", "HTTP_POST"),
            ("/api/v1/led/scenes/cancel", "HTTP_POST"),
            ("/api/v1/led/scenes/save", "HTTP_POST"),
            ("/api/v1/led/scenes/delete", "HTTP_POST"),
            ("/api/v1/led/scenes/export", "HTTP_GET"),
            ("/api/v1/led/scenes/import", "HTTP_POST"),
        )
        for route, method in routes:
            self.assertIn(f'server.on("{route}", {method}', portal)
        self.assertEqual(portal.count("collect_scene_json_body);"), 5)
        self.assertIn("server.clientContentLength()", portal)
        self.assertIn("HTTPRaw &raw = server.raw()", portal)
        collector = portal[portal.index("static void collect_scene_json_body()") :]
        self.assertLess(
            collector.index("if (!scene_json_content_type())"),
            collector.index("HTTPRaw &raw = server.raw()"),
        )
        self.assertIn("if (!scene_request_ready()) return;", portal)
        self.assertIn("scene_body.expected > scene_json::SCENE_JSON_BODY_MAX_BYTES", portal)
        self.assertLess(
            portal.index("scene_body.expected > scene_json::SCENE_JSON_BODY_MAX_BYTES"),
            portal.index("new (std::nothrow) char[scene_body.expected + 1U]"),
        )
        self.assertIn("DeserializationOption::NestingLimit", (ROOT / "src/web/scene_json.cpp").read_text(encoding="utf-8"))
        for code in (
            "length_required",
            "payload_too_large",
            "unsupported_media_type",
            "generation_conflict",
            "recovery_required",
            "storage_full",
            "storage_uncertain",
        ):
            self.assertIn(f'"{code}"', portal + (ROOT / "src/storage/scene_store.cpp").read_text(encoding="utf-8"))
        self.assertIn('features["scenes"] = true', portal)
        self.assertIn('features["scene_import"] = true', portal)
        self.assertIn('doc["scene_registry_version"]', portal)
        self.assertIn('limits["scene_user_id_first"]', portal)
        self.assertIn('JsonObject scene = doc["scene"]', portal)

        for obsolete in (
            "SHOW_PALETTE",
            "random_show_color",
            "shuffle_show_effect_order",
            "prepare_show_effect",
            "show_effect_since_ms",
        ):
            self.assertNotIn(obsolete, ui)
        self.assertIn("SHOW_SCENE_MS", config_h)
        self.assertNotIn("SHOW_EFFECT_MS", config_h)
        self.assertIn('prefs_scenes_instance.begin("dogrgb_scn", false)', nvs)
        self.assertRegex(
            runtime_config, r"uint8_t\s+version\(\)\s*\{\s*return\s+6;\s*\}"
        )
        self.assertIn("CONFIG_RECORD_VERSION = 2", runtime_config)


if __name__ == "__main__":
    unittest.main()
