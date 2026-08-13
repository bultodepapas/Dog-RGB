# SK6812 RGBW Wiring Guide

**Status:** Current recommended topology for the XIAO ESP32-S3 baseline. Verify against the exact strip and power modules before assembly.

## Topology

```text
5 V boost + ----+---------------- strip A VDD
               +---------------- strip B VDD
5 V boost GND --+---------------- strip A GND
                +---------------- strip B GND
                +---------------- XIAO / GNSS / shifter common GND

XIAO D0/GPIO1 -> AHCT/HCT input -> 330–470 Ω -> strip A DIN
XIAO D1/GPIO2 -> AHCT/HCT input -> 330–470 Ω -> strip B DIN
```

Power the level shifter at 5 V and tie unused inputs to defined levels. Place the series resistor near the receiving strip DIN. Confirm the arrow/data direction printed on each strip.

## Why level shifting is recommended

The XIAO outputs 3.3 V logic while a 5 V pixel may require a higher guaranteed input-high threshold. A 74AHCT125/74HCT125-class buffer provides 5 V-compatible output with a 3.3 V input. Direct drive can appear to work on a short bench wire and fail with voltage, temperature, noise, or cable length.

## Power distribution

- Feed each strip in parallel from a properly rated distribution point; do not route strip B current through strip A conductors.
- Use a star/common-ground strategy with short, adequately sized power paths.
- Add about 1,000 µF across 5 V/GND near each strip input, rated above 5 V with correct polarity.
- Consider far-end injection after measuring voltage drop on the real harness.
- Do not feed the full strips through the XIAO regulator or assume USB/board traces can carry their load.
- Size the cell, protection, boost, switch, connectors, and wire from measured peak/continuous current. The theoretical 48-pixel ceiling can exceed 3 A at 5 V.
- Schema 6 enables a provisional whole-device estimated-current limit. Keep it on during bring-up and compare `/dev` with the bench reading, but never treat it as a replacement for an external current limit or measured component margins.

## Bring-up order

1. Battery disconnected: continuity/short/polarity inspection.
2. Power supply with a safe current limit: verify boost output with no load.
3. Power MCU/GNSS only and verify 3.3 V/serial/GNSS.
4. Connect one strip with firmware brightness forced low; verify DIN direction and colors.
5. Connect the second strip and repeat.
6. Increase only to the maximum intended profile while measuring rail sag, current, resets, and temperatures.
7. Flex/wiggle harnesses and test radio/GNSS during LED activity.

## Common symptoms

| Symptom | Likely checks |
| --- | --- |
| Random flicker/wrong colors | Common ground, level shifter family/power/OE, DIN resistor/location, cable length, data direction |
| Reset/brownout on bright pattern | Boost/current limit, cell/protection, connector/wire drop, bulk capacitance, star ground |
| First pixel damaged/unreliable | Hot-plug/transient, missing resistor/capacitor, power/data sequencing |
| GNSS degrades with LEDs | Converter/strip EMI, antenna distance/orientation, ground routing, GNSS decoupling |
| Far end changes color | Voltage drop; measure/inject power rather than guessing |

See [BOM and power budget](bom_power_budget.md) and the full [build guide](manual_de_construccion.en.md).
