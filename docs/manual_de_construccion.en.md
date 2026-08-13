# Dog-RGB Build Guide

**Status:** Current hardware and firmware baseline, reviewed on 2026-08-12. This English page is canonical; the [Spanish guide](manual_de_construccion.es.md) is a convenience translation.

This guide covers the default two-strip prototype built around a Seeed Studio XIAO ESP32-S3, an EBYTE E108-GN02 GNSS receiver, and SK6812 RGBW LEDs. It is a prototype procedure, not a product certification or a substitute for the datasheets of the exact modules you buy.

## Safety boundary

- Use a protected Li-ion cell, a charger designed for that cell chemistry, and a protection/BMS arrangement whose topology you understand.
- A charger and a BMS are not automatically interchangeable or safe in every series connection. Follow their manufacturers' diagrams.
- Never solder, rewire, or continuity-test with the cell connected.
- Never connect a Li-ion cell directly to a 5 V or 3.3 V rail.
- Begin with a current-limited bench supply. Add the battery only after the logic and LED tests pass.
- Do not charge the collar while it is being worn. Stop immediately if the cell, converter, wiring, or enclosure becomes warm, swells, smells unusual, or is mechanically damaged.
- Treat water resistance as unproven until the completed enclosure and cable entries have been tested.

Advanced protection and telemetry are welcome optional additions, but correct cell protection, insulation, strain relief, and current-carrying capacity are baseline requirements.

## Reference configuration

| Area | Default |
| --- | --- |
| Controller | Seeed Studio XIAO ESP32-S3 |
| GNSS | EBYTE E108-GN02, NMEA at 9,600 baud |
| LEDs | Two independent SK6812 RGBW strips, 24 pixels each |
| LED data | Strip A: D0/GPIO1; strip B: D1/GPIO2 |
| Status output | Optional external LED: D2/GPIO3 |
| GNSS serial | Module TX to D7/GPIO44; module RX from D6/GPIO43 if required |
| LED supply | Regulated 5 V rail sized from measured load |
| Logic translation | 74AHCT125 or equivalent 3.3 V-to-5 V buffer recommended |

See the [pin map](../xiao_s3_pin.md), [power-budget worksheet](bom_power_budget.md), and [SK6812 wiring note](sk6812_wiring.md) before laying out the enclosure.

## Parts and tools

At minimum, plan for:

- the controller, GNSS module, two LED strips, protected cell, charger/protection circuit, and regulated power conversion;
- a 74AHCT125-class level shifter, one 330–470 ohm data resistor per strip, local 0.1 uF bypass capacitors, and bulk capacitance near the first LED of each strip;
- wire sized for the measured current, insulated connectors, heat-shrink, strain relief, a diffuser, and a non-conductive enclosure;
- a multimeter, temperature-capable measurement method, soldering tools, and preferably a current-limited bench supply.

The BOM deliberately does not promise a fixed runtime or claim that an arbitrary “3 A” boost module is sufficient. RGBW current depends heavily on brightness and effect. Measure the actual build at startup, steady state, and its brightest intended pattern.

## Logical wiring

```text
protected cell -> approved charger/protection topology -> regulated rails
                                                        |
                                                        +-- XIAO power input
                                                        +-- GNSS supply
                                                        +-- LED 5 V distribution

XIAO D0 / GPIO1 -> level shifter -> 330–470 R -> strip A DIN
XIAO D1 / GPIO2 -> level shifter -> 330–470 R -> strip B DIN
GNSS TX          -> XIAO D7 / GPIO44 (RX)
XIAO D6 / GPIO43 -> GNSS RX, only if the module needs commands
all grounds      -> common low-impedance return
```

Important details:

- Power both strips in parallel. Do not pass the full current through the first strip to feed the second.
- Put each series resistor close to the receiving strip's `DIN`.
- Decouple the level shifter locally and place bulk capacitance close to each strip input.
- Tie unused buffer inputs to a defined level and configure output-enable pins according to the buffer datasheet.
- Keep GNSS and its antenna away from the boost converter, LED power wiring, and high-current loops.
- Confirm the GNSS module's supply voltage from its exact label/datasheet; do not assume every board variant accepts the same input.
- Direct 3.3 V LED data may work on a short bench prototype, but it is outside a robust 5 V logic margin. Treat it as a diagnostic shortcut, not the final build.

## Assembly sequence

### 1. Plan before soldering

1. Draw the exact power topology and connector polarity.
2. Estimate worst-case load with the [power worksheet](bom_power_budget.md).
3. Decide where the antenna, converter, controller, bulk capacitors, switch, and strain relief will sit.
4. Verify that the enclosure has no pressure points or sharp edges facing the dog.

### 2. Validate the power section alone

1. Leave the controller, GNSS, and strips disconnected.
2. Use a current-limited source and verify every regulated rail with a multimeter.
3. Power-cycle it several times and check polarity, idle current, output stability, and converter temperature.
4. Verify the charger/protection behavior using the component documentation. Do not improvise a charge-path test while the collar is worn.

### 3. Bring up controller and GNSS

1. Connect the XIAO using the power input recommended by its vendor for your chosen topology.
2. Connect the common ground and GNSS UART pins shown above.
3. Add local GNSS decoupling and keep its wires short.
4. Flash the firmware and confirm NMEA/parser activity outdoors before adding LED load.

### 4. Add one LED strip

1. Connect the level shifter, its decoupling, one data resistor, and strip A.
2. Start with global brightness near the default `77/255` or lower.
   Keep the schema-6 estimated-current limiter enabled at its provisional 1,000 mA budget; use `/dev` to compare its estimate with the bench reading before changing the model or ceiling.
3. Confirm data direction (`DIN`, not `DOUT`), stable 5 V, correct RGBW output, and acceptable temperature.
4. If pixels flicker or the controller resets, stop and inspect signal level, ground return, voltage drop, and converter headroom.

### 5. Add the second strip

1. Add strip B on its own data line and parallel power branch.
2. Repeat voltage and temperature measurements at both strip inputs.
3. Run the brightest intended configuration long enough to expose thermal or voltage-drop problems.
   The software estimate is not a substitute for the bench supply limit, rail measurements, or component ratings.

### 6. Finish the mechanical assembly

1. Insulate every exposed conductor and secure the boards without compressing the cell.
2. Add strain relief where wires enter the diffuser and enclosure.
3. Keep the antenna area clear of metal, the battery, and noisy power electronics when practical.
4. Close the enclosure only after the complete bench checklist passes.

## Build and flash

From the firmware project:

```powershell
cd Platformio/Dog-RGB
pio run -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor -e seeed_xiao_esp32s3
```

If upload does not start, verify that the USB cable carries data, select the correct serial port, and use the XIAO boot/reset procedure documented by the board vendor.

## Bench acceptance checklist

Do not put the collar on a dog until all applicable checks pass.

- [ ] No short exists between each supply rail and ground.
- [ ] Connector polarity is keyed or clearly marked.
- [ ] Rails remain in tolerance during boot and the brightest intended effect.
- [ ] Peak current and steady-state current are recorded.
- [ ] Converter, cell, wires, connectors, and enclosure stay within a safe measured temperature.
- [ ] GNSS obtains a trusted fix outdoors while LEDs are active.
- [ ] Both strips render without flicker or controller resets.
- [ ] `DogRGB` is reachable at `http://192.168.4.1/` when the AP policy enables it.
- [ ] `/config`, `/wifi`, `/dev`, route export, and configuration persistence work.
- [ ] The default AP password has been changed if the collar will be used near other people.
- [ ] The enclosure has strain relief, no sharp edges, no exposed conductors, and a comfortable fit.
- [ ] The battery cannot be crushed, bent, punctured, or pulled by the collar strap.

For firmware validation commands, see [Testing and simulation](testing.md). For daily operation, see the [User guide](user-guide.md).

## Maintenance

- Inspect the cell, diffuser, seals, wires, and strain relief before each use.
- Retest current and temperature after changing brightness, LED count, effect behavior, power hardware, or enclosure ventilation.
- Dry and inspect the unit after moisture exposure; an IP label on one component does not certify the assembled collar.
- Back up custom configuration values before reflashing or experimenting with storage layouts.
