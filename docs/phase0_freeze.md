# Hardware and Firmware Baseline Decision

**Status:** Current baseline with deviations called out. “Freeze” means the starting configuration, not that physical validation is complete.

Phase 0 execution evidence, including the explicitly unmeasured electrical table, is recorded in [Baseline de Fase 0 — 2026-08-12](baselines/fase-0-2026-08-12.md).

## Selected baseline

| Area | Decision |
| --- | --- |
| MCU | Seeed Studio XIAO ESP32-S3 |
| GNSS | EBYTE E108-GN02 family, UART NMEA RMC + GGA |
| LEDs | SK6812 RGBW, 5 V, one/two independently driven strips; production default two × 24 |
| Cell | One protected 21700 Li-ion cell, exact part not yet frozen in repository |
| Power | Suitable 1S charger/protection plus 5 V boost and 3.3 V supply path; exact modules/topology require schematic/BOM freeze |
| Interface | Local Wi-Fi portal; optional BLE summary code remains disabled by default |

## Pin decision

| Signal | XIAO | GPIO | Direction |
| --- | --- | ---: | --- |
| Strip A data | D0 | 1 | ESP → strip |
| Strip B data | D1 | 2 | ESP → strip |
| External status LED | D2 | 3 | ESP → LED/resistor |
| GNSS RX | D7 | 44 | GNSS TX → ESP RX |
| GNSS TX | D6 | 43 | ESP TX → GNSS RX (optional) |

D6/D7 keep GNSS away from the intended SD/SPI header area. Wokwi uses D9/D10 only for its isolated UART0 console; physical production does not.

## GNSS decision

- UART: 9,600 baud.
- Receiver capability may be up to 10 Hz; normal firmware processes metric samples at 1 Hz.
- Required messages: RMC and GGA.
- Default trusted-fix gates: fix quality ≥1, satellites ≥6, HDOP ≤2.5, GGA age ≤2 s.
- Active threshold: >0.7 km/h.
- Maximum usable speed: 40 km/h.
- Distance segment: adaptive 3–10 m minimum based on HDOP, <50 m maximum.

The older statement “firmware runs GNSS at 10 Hz” is not the current production behavior.

## LED decision

- Recommended AHCT/HCT level shifting at 5 V.
- 330–470 Ω series resistor at each strip DIN.
- Common/star ground and local bulk capacitance near each strip input.
- Default brightness is 77/255. The schema-6 firmware also enables a provisional 1.000 mA whole-device estimated-current limit; it is a model to calibrate, not a measured or guaranteed hardware limit.
- Never power the full strips through the XIAO USB/5 V board path without a separately validated design.

## Storage decision

The custom partition table is part of the baseline. It provides a dedicated 192 KiB route NVS partition and dual application slots. Flash the complete PlatformIO target when installing/changing the table.

## BLE decision

The 16-byte format/UUIDs are frozen in [BLE specification](ble_spec.md), but the feature is disabled in default firmware. Shared-antenna SoftAP/BLE behavior must be validated before changing that default.

## Decisions still open

- Exact cell, holder, charger/protection, boost, regulator, connector, switch, fuse, wire, diffuser, and enclosure part numbers.
- Verified current/thermal/runtime limits and maximum allowed brightness.
- Charging power-path behavior and whether the device may operate while charging (the user guide currently prohibits charging while worn).
- Final water-ingress target and mechanical service/strain-relief approach.
- Whether a battery gauge/current/temperature sensor is worth adding.

Those are P0 tasks, not assumptions. See [BOM and power budget](bom_power_budget.md) and [Work queue](tasks.md).
