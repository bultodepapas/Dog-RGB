# Wokwi Simulation and Debugging

**Status:** Current repository workflow, reviewed on 2026-08-12. The simulator runs the real `wokwi` firmware environment with controlled GNSS input and digital instrumentation.

Wokwi is useful for firmware integration, parser faults, persistence, modes, automated resets, UART decoding, and LED-bus observation. It cannot establish battery safety, RGBW color accuracy, antenna performance, waterproofing, thermal behavior, or mechanical comfort.

For the complete repository test matrix, see [`../../../docs/testing.md`](../../../docs/testing.md).

## Simulated system

- Seeed XIAO ESP32-S3 model with 8 MB flash and 8 MB octal PSRAM.
- Two virtual 24-pixel WS2812 strips on D0/GPIO1 and D1/GPIO2.
- External status LED on D2/GPIO3.
- Custom NMEA GNSS chip transmitting at 9,600 baud into D7/GPIO44.
- Simulation console on UART0 through D9/GPIO8 and D10/GPIO9.
- Five-channel logic analyzer for both LED buses, GNSS UART, GNSS scheduler tick, and status LED.
- Optional HTTP forwarding to `http://localhost:8180` with the Private IoT Gateway.
- RFC2217 serial access on port 4000 and GDB on port 3333.

The physical target keeps production USB CDC and normal LED refresh behavior. Only the `wokwi` environment routes the console to UART0 and throttles virtual WS2812 transport to 5 FPS; effect calculations retain their normal 50 ms cadence.

The virtual strip is RGB WS2812, not RGBW SK6812. Interpret its animation/timing, not its white-channel output or electrical load.

## Files

| Path | Purpose |
| --- | --- |
| `diagram.json` | Canonical circuit and instrumentation |
| `wokwi.toml` | Firmware image, forwarding, RFC2217, and GDB settings |
| `chips/nmea-gps.chip.c` | Controllable GNSS custom chip |
| `wokwi/*.test.yaml` | Automated scenarios |
| `tools/wokwi.ps1` | Prepare, lint, run, and analyze wrapper |
| `tools/wokwi_diagram.py` | Generates full or GNSS-only capture diagrams |
| `tools/analyze_wokwi.py` | Serial/VCD assertions and JSON summary |
| `.vscode/wokwi-gdb.launch.example.json` | Local VS Code GDB template |

Generated `artifacts/` and the custom-chip WASM output are disposable and must not contain credentials.

## Prerequisites

1. PlatformIO Core with the repository's pinned pioarduino platform.
2. The official Wokwi CLI available as `wokwi-cli` or in `%USERPROFILE%\.wokwi\bin`.
3. Wokwi for VS Code and an appropriate license for interactive simulation.
4. `WOKWI_CLI_TOKEN` for automated CLI scenarios.

Copy `.env.example` to `.env` if you want the PowerShell wrapper to load the token locally. `.env` is ignored by Git. Never commit a real token.

## Prepare the simulator

From `Platformio/Dog-RGB`:

```powershell
.\tools\wokwi.ps1 -Action prepare
```

The command:

1. compiles `chips/nmea-gps.chip.c` to WASM;
2. builds `env:wokwi`;
3. runs the official diagram linter.

Build and host-test the physical image separately:

```powershell
pio run -e seeed_xiao_esp32s3
python -m unittest discover -s test -p "test_*.py" -v
```

## Interactive use

1. Run `prepare`.
2. Open `diagram.json` in VS Code.
3. Run **Wokwi: Start Simulator**.
4. Watch the two strips, status LED, serial monitor, and **Chips Console**.
5. Select the `gnss` chip and change its attributes without recompiling.

### GNSS controls

| Attribute | Range | Use |
| --- | ---: | --- |
| `profile` | `0..10` | Select a normal or fault condition |
| `speedKph` | `0..40` | Cross the ten LED speed ranges |
| `rateHz` | `1..5` | Exercise UART/parser throughput |
| `utcHour` | `0..23` | Test UTC-5 local time and Day Mode |
| `positionM` | `0..900` | Exercise distance, geofence, or position jumps |

| Profile | Emission | Behavior under test |
| ---: | --- | --- |
| 0 | Valid moving GGA + RMC | Normal flow and recovery |
| 1 | Valid stationary fix | Idle/range 1 |
| 2 | RMC `V` + no-fix GGA | Fix loss |
| 3 | HDOP 9.9 | Quality rejection |
| 4 | Silent UART | Missing receiver/stale data |
| 5 | Two satellites | Satellite threshold |
| 6 | RMC only | GGA expiration |
| 7 | GGA only | RMC expiration |
| 8 | Invalid checksum | NMEA transport rejection |
| 9 | Malformed fields, valid checksum | Parser rejection |
| 10 | 80 km/h | Implausible-speed filtering |

The chip advances time and position consistently with `rateHz` and `speedKph`. Its console reports attribute changes and cumulative scheduler/transport diagnostics.

## Simulation console

Interactive sessions accept the same commands used by the scenarios:

```text
sim mode speed
sim mode geofence
sim mode show
sim mode simple
sim day on
sim day off
sim home here
sim home clear
sim leds off
sim leds on
sim status
sim reboot
sim help
```

Mode, Day Mode, and Home commands call the real configuration/storage paths. `sim reboot` calls `ESP.restart()` to exercise persistence. `sim leds off` suppresses only the virtual LED transmission to speed long GNSS scenarios; state, effects, and logs continue to update. These commands are compiled only for the Wokwi target.

## Automated scenarios

```powershell
# Boot, AP, GNSS, movement, and digital buses
.\tools\wokwi.ps1 -Action test -Scenario wokwi/boot.test.yaml

# LED modes, geofence, Day Mode, and persistence
.\tools\wokwi.ps1 -Action test -Scenario wokwi/modes.test.yaml -TimeoutMs 90000

# Session journal across resets
.\tools\wokwi.ps1 -Action test -Scenario wokwi/session-persistence.test.yaml -TimeoutMs 45000

# Basic GNSS quality/recovery profiles
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-profiles.test.yaml -TimeoutMs 60000

# Transport, parser, and quality fault matrix
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-faults.test.yaml -TimeoutMs 90000

# Implausible speed and recovery
.\tools\wokwi.ps1 -Action test -Scenario wokwi/speed-validity.test.yaml -TimeoutMs 25000

# Per-subsystem loop latency
.\tools\wokwi.ps1 -Action test -Scenario wokwi/loop-diagnostics.test.yaml -TimeoutMs 20000

# 5 Hz input, range boundaries, and position jump
.\tools\wokwi.ps1 -Action test -Scenario wokwi/gps-rate-ranges.test.yaml -TimeoutMs 60000

# Discover and run every *.test.yaml after one preparation
.\tools\wokwi.ps1 -Action suite -TimeoutMs 90000
```

The wrapper retries only recognized transient backend WebSocket closures, up to three attempts. Assertion failures and firmware errors are not retried or hidden.

### Capture profiles

`-CaptureProfile auto` is the default:

- `boot.test` uses `full`, retaining LED, GNSS, and status channels;
- longer scenarios use `gnss`, removing high-frequency LED channels so the VCD buffer reaches the end of the test.

Override with `-CaptureProfile full` or `gnss` when investigating a specific issue. Generated diagrams preserve `diagram.json` as the single canonical circuit.

Each scenario writes:

```text
artifacts/<scenario>.serial.log
artifacts/<scenario>.vcd
artifacts/<scenario>.analysis.json
artifacts/<scenario>.diagram.json
```

`analyze_wokwi.py` rejects crashes, control errors, UART overflow, missing required signals, undecodable UART, failed semantic assertions, and configured latency regressions. It decodes NMEA checksums/rates and reports loop, heap, GNSS, motion, LED, radio, and logger evidence.

## Diagnostic log families

| Prefix | Contents |
| --- | --- |
| `[BOOT]` | Reset cause and enabled features |
| `[GPS_LINK]` | Bytes, sentences, checksum/parser failures, peaks, stale ages, overflow |
| `[GPS_FIX]` | Raw/trusted/current state and rejection reason |
| `[GPS_ANOMALY]` | New anomalies since the previous report |
| `[MOTION]` | Filtered speed, usability, range, distance, segment decision |
| `[LED]` | Render decision, effect, speed, intensity, and Day Mode |
| `[SYS]` | Heap, loop work, logger cost, and per-subsystem maxima |
| `[WIFI]`, `[WIFI_DIAG]` | Radio state, clients, transitions, and counters |
| `[SIM_CTRL]`, `[SIM_STATE]` | Automation command and resulting state |
| `[nmea-gps]` | Custom-chip scheduler and UART diagnostics |

Prefer the generated analysis JSON over fragile text matching when comparing runs. Important invariants include zero UART overflow/fatal markers, stable minimum heap, bounded loop work, and NMEA counts consistent with the selected rate.

## GDB

`wokwi.toml` exposes GDB on `localhost:3333`.

1. Run `prepare` to create `firmware.elf` with symbols.
2. Copy or merge `.vscode/wokwi-gdb.launch.example.json` into the ignored local `.vscode/launch.json`.
3. Start Wokwi and wait for the firmware to run.
4. Start the **Wokwi ESP32-S3 GDB** launch profile.
5. Set breakpoints in `gps.cpp`, `led_ui.cpp`, `wifi_mgr.cpp`, or `wokwi_control.cpp`.

A breakpoint stops the simulated MCU while external/custom-chip state may continue evolving. Restart a scenario after a long pause before drawing conclusions about stale-data ages.

## Portal and network

Enable the Private Wokwi IoT Gateway to expose the configured HTTP forwarding at `http://localhost:8180`. The public gateway permits device egress but not incoming access to the portal. A private gateway can also support packet capture for DNS/HTTP investigation.

Do not put real Wi-Fi credentials or secrets into shared simulations. RFC2217 uses port 4000; port 3333 remains reserved for GDB.

## Logic analyzer

| Channel | Signal |
| --- | --- |
| `logic.D0` | Strip A data |
| `logic.D1` | Strip B data |
| `logic.D2` | GNSS TX/UART |
| `logic.D3` | GNSS scheduler tick |
| `logic.D4` | External status LED |

Open VCD files with PulseView or GTKWave. Decode D2 as UART 9600-8N1 and D0/D1 as WS2812 when using the full capture. Do not downsample the file passed to the automated analyzer because it verifies individual UART bits and checksums.

## What must still be tested physically

- SK6812 RGBW channel order and appearance;
- battery, charger/BMS, converter efficiency, current limits, voltage drop, and brownout margin;
- wire/connector/enclosure temperatures at sustained brightness;
- GNSS antenna orientation, acquisition, multipath, and LED/converter interference;
- Wi-Fi range, phone captive-portal behavior, and representative RF coexistence;
- water ingress, flex fatigue, strain relief, impact, fit, and comfort.

Use the [build guide](../../../docs/manual_de_construccion.en.md) for the physical acceptance sequence.

## External references

- [Wokwi CLI](https://docs.wokwi.com/wokwi-ci/cli-usage)
- [Automation scenarios](https://docs.wokwi.com/wokwi-ci/automation-scenarios)
- [Project configuration](https://docs.wokwi.com/vscode/project-config)
- [VS Code debugging](https://docs.wokwi.com/vscode/debugging)
- [Logic analyzer](https://docs.wokwi.com/guides/logic-analyzer)
- [ESP32 Wi-Fi simulation](https://docs.wokwi.com/guides/esp32-wifi)
- [Custom chips](https://docs.wokwi.com/chips-api/getting-started)

Wokwi APIs and CLI output can evolve. When upgrading the CLI, run the linter, host contract suite, and all eight scenarios before accepting the change.
