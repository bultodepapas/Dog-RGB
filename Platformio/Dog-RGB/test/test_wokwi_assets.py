import json
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class WokwiAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.diagram = json.loads((PROJECT_ROOT / "diagram.json").read_text(encoding="utf-8"))
        cls.parts = {part["id"]: part for part in cls.diagram["parts"]}
        cls.connections = {
            (connection[0], connection[1]) for connection in cls.diagram["connections"]
        }

    def test_esp32_s3_keeps_monitor_off_gnss_pins(self):
        xiao = self.parts["xiao"]
        self.assertEqual(xiao["type"], "board-xiao-esp32-s3")
        self.assertNotIn("serialInterface", xiao["attrs"])
        monitor_connections = {
            pair for pair in self.connections if any("$serialMonitor" in endpoint for endpoint in pair)
        }
        self.assertEqual(
            monitor_connections,
            {
                ("$serialMonitor:RX", "xiao:D10"),
                ("$serialMonitor:TX", "xiao:D9"),
            },
        )
        self.assertFalse(any("D6" in endpoint or "D7" in endpoint for pair in monitor_connections for endpoint in pair))

    def test_firmware_pin_mapping_is_represented(self):
        self.assertIn(("xiao:D0", "strip_a:DIN"), self.connections)
        self.assertIn(("xiao:D1", "strip_b:DIN"), self.connections)
        self.assertIn(("xiao:D2", "status_led:A"), self.connections)
        self.assertIn(("gnss:TX", "xiao:D7"), self.connections)

    def test_led_strips_have_power_connections(self):
        for strip in ("strip_a", "strip_b"):
            self.assertIn(("xiao:5V", f"{strip}:VDD"), self.connections)
            self.assertIn(("xiao:GND", f"{strip}:VSS"), self.connections)

    def test_logic_analyzer_captures_both_led_buses_and_gnss(self):
        self.assertIn(("xiao:D0", "logic:D0"), self.connections)
        self.assertIn(("xiao:D1", "logic:D1"), self.connections)
        self.assertIn(("gnss:TX", "logic:D2"), self.connections)
        self.assertIn(("gnss:DEBUG", "logic:D3"), self.connections)
        self.assertIn(("xiao:GND", "logic:GND"), self.connections)

    def test_wokwi_config_targets_wokwi_platformio_environment(self):
        config = (PROJECT_ROOT / "wokwi.toml").read_text(encoding="utf-8")
        self.assertIn('firmware = ".pio/build/wokwi/firmware.bin"', config)
        self.assertIn('elf = ".pio/build/wokwi/firmware.elf"', config)
        self.assertIn('vcdFile = "artifacts/wokwi.vcd"', config)
        platformio = (PROJECT_ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("[env:wokwi]", platformio)
        self.assertIn("extends = env:seeed_xiao_esp32s3", platformio)
        self.assertIn("-DARDUINO_USB_CDC_ON_BOOT=0", platformio)
        self.assertIn("-DDOG_RGB_WOKWI_SIM=1", platformio)
        self.assertIn("build_unflags", platformio)

    def test_wokwi_console_is_compile_time_isolated_from_physical_build(self):
        source = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
        pins = (PROJECT_ROOT / "include/pins.h").read_text(encoding="utf-8")
        self.assertIn("#if defined(DOG_RGB_WOKWI_SIM)", source)
        self.assertIn("PIN_WOKWI_SERIAL_RX, PIN_WOKWI_SERIAL_TX", source)
        self.assertIn("PIN_WOKWI_SERIAL_RX = 8", pins)
        self.assertIn("PIN_WOKWI_SERIAL_TX = 9", pins)

    def test_gnss_controls_match_custom_chip_attributes(self):
        definition = json.loads(
            (PROJECT_ROOT / "chips/nmea-gps.chip.json").read_text(encoding="utf-8")
        )
        controls = {control["id"] for control in definition["controls"]}
        self.assertEqual(controls, {"profile", "speedKph"})
        source = (PROJECT_ROOT / "chips/nmea-gps.chip.c").read_text(encoding="utf-8")
        self.assertIn('attr_init("profile", PROFILE_MOVING)', source)
        self.assertIn('attr_init("speedKph", 12U)', source)
        self.assertIn('"$%s*%02X\\r\\n"', source)
        self.assertIn("timer_start(chip->timer, 1000000, true)", source)

    def test_gnss_uart_uses_backend_safe_unconnected_rx(self):
        definition = json.loads(
            (PROJECT_ROOT / "chips/nmea-gps.chip.json").read_text(encoding="utf-8")
        )
        self.assertEqual(definition["pins"], ["TX", "RX", "DEBUG"])
        source = (PROJECT_ROOT / "chips/nmea-gps.chip.c").read_text(encoding="utf-8")
        self.assertIn('.tx = pin_init("TX", INPUT_PULLUP)', source)
        self.assertIn('.rx = pin_init("RX", INPUT)', source)
        self.assertNotIn(".rx = NO_PIN", source)
        self.assertFalse(any("gnss:RX" in endpoint for pair in self.connections for endpoint in pair))

    def test_custom_chip_wasm_is_present(self):
        wasm = (PROJECT_ROOT / "chips/nmea-gps.chip.wasm").read_bytes()
        self.assertGreater(len(wasm), 1024)
        self.assertEqual(wasm[:4], b"\x00asm")

    def test_scenarios_cover_boot_and_quality_recovery(self):
        boot = (PROJECT_ROOT / "wokwi/boot.test.yaml").read_text(encoding="utf-8")
        profiles = (PROJECT_ROOT / "wokwi/gps-profiles.test.yaml").read_text(encoding="utf-8")
        self.assertIn("trusted=1 current=1 reason=ok", boot)
        self.assertIn("control: profile", profiles)
        self.assertIn("reason=rmc_v", profiles)
        self.assertIn("reason=hdop", profiles)

    def test_helper_loads_ignored_dotenv_without_overriding_process_environment(self):
        helper = (PROJECT_ROOT / "tools/wokwi.ps1").read_text(encoding="utf-8")
        ignore = (PROJECT_ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("Import-DotEnv -Path (Join-Path $projectRoot '.env')", helper)
        self.assertIn("GetEnvironmentVariable($name, 'Process')", helper)
        self.assertIn(".env\n", ignore.replace("\r\n", "\n"))


if __name__ == "__main__":
    unittest.main()
