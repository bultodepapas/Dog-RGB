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

    def test_logic_analyzer_captures_leds_gnss_and_has_long_run_capacity(self):
        self.assertIn(("xiao:D0", "logic:D0"), self.connections)
        self.assertIn(("xiao:D1", "logic:D1"), self.connections)
        self.assertIn(("gnss:TX", "logic:D2"), self.connections)
        self.assertIn(("gnss:DEBUG", "logic:D3"), self.connections)
        self.assertIn(("xiao:D2", "logic:D4"), self.connections)
        self.assertIn(("xiao:GND", "logic:GND"), self.connections)
        self.assertEqual(self.parts["logic"]["attrs"]["bufferSize"], "1000000")

    def test_wokwi_config_targets_wokwi_platformio_environment(self):
        config = (PROJECT_ROOT / "wokwi.toml").read_text(encoding="utf-8")
        self.assertIn('firmware = ".pio/build/wokwi/firmware.bin"', config)
        self.assertIn('elf = ".pio/build/wokwi/firmware.elf"', config)
        self.assertIn('vcdFile = "artifacts/wokwi.vcd"', config)
        self.assertIn("gdbServerPort = 3333", config)
        platformio = (PROJECT_ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("[env:wokwi]", platformio)
        self.assertIn("extends = env:seeed_xiao_esp32s3", platformio)
        self.assertIn("-DARDUINO_USB_CDC_ON_BOOT=0", platformio)
        self.assertIn("-DDOG_RGB_WOKWI_SIM=1", platformio)
        self.assertIn("-DDOG_RGB_WOKWI_LED_SHOW_MS=200", platformio)
        self.assertIn("monitor_speed = 460800", platformio)
        self.assertIn("build_unflags", platformio)

    def test_wokwi_throttles_only_led_transport_not_effect_tick(self):
        source = (PROJECT_ROOT / "src/led/led_ui.cpp").read_text(encoding="utf-8")
        self.assertIn("#if defined(DOG_RGB_WOKWI_LED_SHOW_MS)", source)
        self.assertIn("now_ms - last_transport_ms < DOG_RGB_WOKWI_LED_SHOW_MS", source)
        self.assertIn("void tick()", source)
        self.assertIn("update_led_ui();", source)

    def test_wokwi_console_is_compile_time_isolated_from_physical_build(self):
        source = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
        pins = (PROJECT_ROOT / "include/pins.h").read_text(encoding="utf-8")
        config = (PROJECT_ROOT / "include/config.h").read_text(encoding="utf-8")
        self.assertIn("#if defined(DOG_RGB_WOKWI_SIM)", source)
        self.assertIn("PIN_WOKWI_SERIAL_RX, PIN_WOKWI_SERIAL_TX", source)
        self.assertIn("Serial.begin(CONSOLE_BAUD", source)
        self.assertIn("CONSOLE_BAUD = 460800", config)
        self.assertIn("CONSOLE_BAUD = 115200", config)
        self.assertIn("PIN_WOKWI_SERIAL_RX = 8", pins)
        self.assertIn("PIN_WOKWI_SERIAL_TX = 9", pins)

    def test_gnss_controls_match_custom_chip_attributes(self):
        definition = json.loads(
            (PROJECT_ROOT / "chips/nmea-gps.chip.json").read_text(encoding="utf-8")
        )
        controls = {control["id"] for control in definition["controls"]}
        self.assertEqual(
            controls, {"profile", "speedKph", "rateHz", "utcHour", "positionM"}
        )
        source = (PROJECT_ROOT / "chips/nmea-gps.chip.c").read_text(encoding="utf-8")
        self.assertIn('attr_init("profile", PROFILE_MOVING)', source)
        self.assertIn('attr_init("speedKph", 12U)', source)
        self.assertIn('attr_init("rateHz", 1U)', source)
        self.assertIn('attr_init("utcHour", 20U)', source)
        self.assertIn('attr_init("positionM", 0U)', source)
        self.assertIn('"$%s*%02X\\r\\n"', source)
        self.assertIn("timer_start(chip->timer, 1000000U, false)", source)
        self.assertIn("PROFILE_BAD_CHECKSUM", source)
        self.assertIn("PROFILE_MALFORMED", source)
        self.assertIn("PROFILE_SPEED_SPIKE", source)

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
        faults = (PROJECT_ROOT / "wokwi/gps-faults.test.yaml").read_text(encoding="utf-8")
        rates = (PROJECT_ROOT / "wokwi/gps-rate-ranges.test.yaml").read_text(encoding="utf-8")
        validity = (PROJECT_ROOT / "wokwi/speed-validity.test.yaml").read_text(encoding="utf-8")
        modes = (PROJECT_ROOT / "wokwi/modes.test.yaml").read_text(encoding="utf-8")
        sessions = (PROJECT_ROOT / "wokwi/session-persistence.test.yaml").read_text(encoding="utf-8")
        self.assertIn("trusted=1 current=1 reason=ok", boot)
        self.assertIn("session_hist=2 session_slot=", sessions)
        self.assertIn("session_save_failures=0 session_recoveries=1", sessions)
        self.assertIn("control: profile", profiles)
        self.assertIn("reason=rmc_v", profiles)
        self.assertIn("reason=hdop", profiles)
        self.assertIn("checksum_seen=1", faults)
        self.assertIn("reason=gga_stale", faults)
        self.assertIn("reason=rmc_stale", faults)
        self.assertIn("speed_spike_seen=1", faults)
        self.assertIn("usable=0 active=0 range=1", faults)
        self.assertIn("control: rateHz", rates)
        self.assertIn("range=10", rates)
        self.assertIn("large_seg_total=1", rates)
        self.assertIn("usable=0 active=0 range=1", validity)
        self.assertIn("speed_kph=12.00 usable=1 active=1 range=7", validity)
        for mode in ("simple", "show", "geofence", "speed"):
            self.assertIn(f"sim mode {mode}", modes)
        self.assertIn("sim reboot", modes)
        self.assertIn("sim day on", modes)

    def test_simulation_control_channel_is_wokwi_only(self):
        source = (PROJECT_ROOT / "src/sim/wokwi_control.cpp").read_text(encoding="utf-8")
        main = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("#if defined(DOG_RGB_WOKWI_SIM)", source)
        self.assertIn('strncmp(line, "sim ", 4)', source)
        self.assertIn("config::save()", source)
        self.assertIn("ESP.restart()", source)
        self.assertIn("handle_leds", source)
        self.assertIn("led_ui::set_transport_enabled(enabled)", source)
        self.assertIn("mode != previous.mode", source)
        self.assertIn("enabled != previous.day_mode_enabled", source)
        self.assertGreaterEqual(source.count('Serial.print(" changed=");'), 2)
        self.assertIn("wokwi_control::tick();", main)

    def test_speed_validity_is_preserved_after_filtering(self):
        header = (PROJECT_ROOT / "include/gps/gps.h").read_text(encoding="utf-8")
        gps = (PROJECT_ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        main = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
        portal = (PROJECT_ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")
        self.assertIn("bool speed_usable();", header)
        self.assertIn("last_speed_usable_val = speed_usable;", gps)
        self.assertIn("last_speed_usable_val = false;", gps)
        self.assertIn('Serial.print(gps::speed_usable() ? "1" : "0");', main)
        self.assertGreaterEqual(portal.count('gps["speed_usable"]'), 2)

    def test_loop_diagnostics_separate_application_work_from_serial_logging(self):
        main = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
        analyzer = (PROJECT_ROOT / "tools/analyze_wokwi.py").read_text(encoding="utf-8")
        self.assertIn("loop_elapsed_us - log_elapsed_us", main)
        self.assertIn('Serial.print(" loop_work_max_us=");', main)
        self.assertIn('Serial.print(" log_emit_max_us=");', main)
        self.assertIn("class PeriodicLogWriter : public Print", main)
        self.assertIn("class SerialLogQueue : public Print", main)
        self.assertIn("uint8_t buffer_[512]", main)
        self.assertIn("sink_.write(buffer_, used_);", main)
        self.assertIn("uint8_t buffer_[4096]", main)
        self.assertIn("sink.availableForWrite()", main)
        self.assertIn("MAX_DRAIN_BYTES_PER_TICK = 64", main)
        self.assertIn("log_drop_bytes=", main)
        self.assertIn("log_slot_max_us[7]", main)
        self.assertIn("wifi_mgr::ap_ip().toString()", main)
        portal = (PROJECT_ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")
        wifi_mgr = (PROJECT_ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        self.assertNotIn("WiFi.softAPIP()", main)
        self.assertNotIn("WiFi.softAPIP()", portal)
        self.assertIn("ap_ip_state = WiFi.softAPIP();", wifi_mgr)
        self.assertNotIn("WiFi.getMode()", main)
        self.assertNotIn("WiFi.getMode()", portal)
        self.assertIn("wifi_mode_state", wifi_mgr)
        for phase in ("gps", "control", "geofence", "storage", "radio", "led", "http"):
            self.assertIn(f'Serial.print(" {phase}_max_us=");', main)
        self.assertIn('"maximum_reported_loop_work_us"', analyzer)
        self.assertIn('"maximum_reported_log_emit_us"', analyzer)
        self.assertIn('"maximum_reported_phase_us"', analyzer)
        wifi = (PROJECT_ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        self.assertIn("ap_station_poll_max_us", wifi)
        self.assertIn("channel_query_max_us", wifi)
        self.assertIn("read_ap_station_count()", wifi)
        self.assertIn("read_wifi_channel()", wifi)

    def test_helper_exports_and_analyzes_every_scenario(self):
        helper = (PROJECT_ROOT / "tools/wokwi.ps1").read_text(encoding="utf-8")
        self.assertIn("'suite'", helper)
        self.assertIn("'--vcd-file', $vcdLog", helper)
        self.assertIn("'--diagram-file', $diagramFile", helper)
        self.assertIn("tools/wokwi_diagram.py", helper)
        self.assertIn("Invoke-WokwiScenario", helper)
        self.assertIn("Connection to transport closed unexpectedly", helper)
        self.assertIn("$transientTransportFailure", helper)
        self.assertIn("--capture-profile", helper)
        self.assertIn("tools/analyze_wokwi.py", helper)
        analyzer = (PROJECT_ROOT / "tools/analyze_wokwi.py").read_text(encoding="utf-8")
        self.assertIn("decode_uart_8n1", analyzer)
        self.assertIn("nmea_checksum_counts", analyzer)
        self.assertIn("ws2812_bursts", analyzer)

    def test_gdb_example_targets_wokwi_elf_and_esp32s3_toolchain(self):
        launch = json.loads(
            (PROJECT_ROOT / ".vscode/wokwi-gdb.launch.example.json").read_text(
                encoding="utf-8"
            )
        )
        config = launch["configurations"][0]
        self.assertIn(".pio/build/wokwi/firmware.elf", config["program"])
        self.assertIn("xtensa-esp32s3-elf-gdb", config["miDebuggerPath"])
        self.assertEqual(config["miDebuggerServerAddress"], "localhost:3333")

    def test_helper_loads_ignored_dotenv_without_overriding_process_environment(self):
        helper = (PROJECT_ROOT / "tools/wokwi.ps1").read_text(encoding="utf-8")
        ignore = (PROJECT_ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("Import-DotEnv -Path (Join-Path $projectRoot '.env')", helper)
        self.assertIn("GetEnvironmentVariable($name, 'Process')", helper)
        self.assertIn(".env\n", ignore.replace("\r\n", "\n"))


if __name__ == "__main__":
    unittest.main()
