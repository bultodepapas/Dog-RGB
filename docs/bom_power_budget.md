# BOM and Power-Budget Worksheet

**Status:** Planning document, not measured evidence. Exact power modules and the physical build are not yet frozen.

The current empty measurement record and safe execution sequence are tracked in [Baseline de Fase 0 — 2026-08-12](baselines/fase-0-2026-08-12.md); estimated values below must not be copied into that record as observations.

## Base BOM

### Control and sensing

- Seeed Studio XIAO ESP32-S3.
- EBYTE E108-GN02 GNSS module and appropriate antenna.
- External status LED and resistor (optional but used by current pin map).

### Lighting

- One or two 5 V SK6812 RGBW strips; default firmware expects two × 24 pixels.
- 74AHCT125/74HCT125-class level shifter powered at 5 V.
- 330–470 Ω series resistor per data input.
- About 1,000 µF bulk capacitor near each strip input, plus local ceramic decoupling where practical.

### Power and mechanical

- Known, protected 21700 Li-ion cell with a datasheet and current rating.
- 1S charger/protection/BMS solution compatible with that cell.
- 5 V boost converter with verified continuous/peak load, efficiency, thermal behavior, and low-voltage cutoff interaction.
- Defined 3.3 V path for MCU/GNSS.
- Rated switch, fuse/protection as required by the final topology, connectors, and silicone wire.
- Enclosure, diffuser, seals, strain relief, and cell restraint.

Do not select modules by marketplace headline ratings alone. Record exact part numbers and datasheets in the hardware revision.

## Design ceiling

RGBW addressable pixels can draw far more than a typical animated pattern. A conservative planning ceiling of 60–80 mA per pixel at full multi-channel output gives:

```text
48 pixels × 0.060–0.080 A = 2.88–3.84 A at 5 V
LED power = 14.4–19.2 W
```

MCU/GNSS/radio and conversion loss are additional. Therefore a nominal “3 A boost” may be acceptable only with an enforced/verified animation and brightness envelope; it is not automatically sufficient for unrestricted full-white output.

The current firmware defaults to roughly 30% brightness and animated content, but that does not create a calibrated hard current limit.

## Battery-side calculation

Use measured 5 V output power and converter efficiency:

```text
P_out_W = 5 V × measured_5V_current_A
I_cell_A = P_out_W / (cell_voltage_V × efficiency)
ideal_runtime_h = usable_cell_capacity_Ah / average_I_cell_A
```

Example only—not a project measurement:

```text
measured 5 V average = 0.75 A
P_out = 3.75 W
cell voltage = 3.7 V, efficiency = 85%
I_cell ≈ 1.19 A
5.0 Ah / 1.19 A ≈ 4.2 h ideal
```

Real usable runtime is lower/variable because capacity depends on load, cutoff, temperature, cell age, wiring, converter efficiency curve, radio duty, GNSS acquisition, and effect/Day Mode profile.

The previous 7–18 hour estimates were unmeasured and are no longer treated as requirements evidence.

## Measurement matrix

For each hardware revision, measure at both the cell and 5 V output:

| Profile | Brightness/mode | Radio/GNSS state | Record |
| --- | --- | --- | --- |
| Boot/acquisition | Default | AP active, GNSS searching | peak/average current, rail minima, reset reason |
| Quiet status | Effect body off | AP and STA variants | average current |
| Typical walk | Declared Speed/Geofence profile | trusted GNSS, chosen radio state | average/peak current and temperature |
| Show | Default and maximum intended | AP traffic | average/peak current and temperature |
| Electrical ceiling | Safely controlled test pattern | worst intended radio | rail drop, converter/wire/connector temperature |
| Day Mode | Active window | same radio/GNSS profile | savings relative to matching effects-on profile |

Log ambient temperature, instrument, sample interval, cell voltage/state, firmware commit, duration, and exact LED data. Measure after thermal equilibrium, not only for a few seconds.

## Acceptance questions

- Does the cell's continuous/peak rating exceed measured input with margin?
- Does protection trip correctly without becoming the normal current limiter?
- Do 5 V/3.3 V rails stay within component limits during LED/radio transients?
- Do boost, cell, connectors, wiring, charger/BMS, MCU, and strips stay below defined temperature limits?
- Is runtime at least the stated requirement under a reproducible “typical” profile?
- Does GNSS quality remain acceptable with the converter and strips active?

Until these are answered, runtime and thermal numbers remain estimates. See [Build guide](manual_de_construccion.en.md) and [Requirements](requirements.md).
