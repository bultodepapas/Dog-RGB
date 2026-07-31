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

    def test_esp32_s3_uses_usb_serial_jtag(self):
        xiao = self.parts["xiao"]
        self.assertEqual(xiao["type"], "board-xiao-esp32-s3")
        self.assertEqual(xiao["attrs"]["serialInterface"], "USB_SERIAL_JTAG")
        self.assertFalse(
            any("$serialMonitor" in endpoint for pair in self.connections for endpoint in pair),
            "USB CDC must not share the GNSS UART pins",
        )

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
        self.assertIn(("xiao:GND", "logic:GND"), self.connections)

    def test_wokwi_config_targets_wokwi_platformio_environment(self):
        config = (PROJECT_ROOT / "wokwi.toml").read_text(encoding="utf-8")
        self.assertIn('firmware = ".pio/build/wokwi/firmware.bin"', config)
        self.assertIn('elf = ".pio/build/wokwi/firmware.elf"', config)
        self.assertIn('vcdFile = "artifacts/wokwi.vcd"', config)
        platformio = (PROJECT_ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("[env:wokwi]", platformio)
        self.assertIn("extends = env:seeed_xiao_esp32s3", platformio)

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


if __name__ == "__main__":
    unittest.main()
