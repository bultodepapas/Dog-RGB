# Build Manual - Smart LED Dog Collar (MVP)

[Espanol](manual_de_construccion.es.md) | [English](manual_de_construccion.en.md)

Complete guide to build the prototype with solid electronics practices.

---

## 1) Scope and safety

- Covers the Phase 1 MVP: GPS + Wi-Fi portal + SK6812 LEDs.
- Basic electronics and soldering skills required.
- Do not assemble with the battery connected.
- Verify polarity and continuity before powering on.

---

## 2) Bill of materials (Base BOM)

### Main electronics
- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz, BDS/GPS/GLONASS)
- LEDs: SK6812 (5V, single-wire), 2 strips (24 LEDs each)
- Battery: 21700 Li-ion (~5000 mAh)
- BMS 1S + USB-C charger
- 5V boost >= 3A continuous
- 3.3V regulator (if not provided by the MCU)

### Level shifting and protection
- 5V level shifter: 74AHCT125 (or equivalent)
- No level shifter option (prototype): direct data with short wires
- Series resistors: 330-470 ohm (2 units, one per data line)
- Capacitor: 1000 uF electrolytic, >=6.3V (1 unit, near first LED)
- Capacitor: 0.1 uF ceramic (1 optional unit, near first LED)
- GNSS capacitors: 10-47 uF + 0.1 uF ceramic (1 set, near the module)

### Mechanical
- Nylon strap (20-25 mm width)
- Silicone diffuser tube
- IP65-IP67 enclosure with strain relief
- Silicone wire (AWG 22-24 for 5V/LEDs, AWG 26-28 for signals)

### Extras
- External status LED + resistor
- Physical switch or Hall sensor (optional)
- Heat-shrink, double-sided tape, epoxy adhesive

---

## 3) Recommended tools

- Soldering iron and solder
- Multimeter (continuity and voltage)
- Tweezers and cutters
- Heat gun (heat-shrink)
- Bench power supply (optional, recommended)

---

## 4) Wiring diagram (reference)

See the diagram in `docs/manual_de_uso.md`.
Official XIAO ESP32-S3 pinout reference: `xiao_s3_pin.md`.

---

## 5) Preparation

1) Cut strap and diffuser to length.
2) Prepare wires (short strip, pre-tin).
3) Label 5V, GND, and data lines.
4) Plan wire routing inside diffuser and enclosure.

---

## 6) Step-by-step assembly

### Step 1: Power system
1) Connect battery -> BMS -> USB-C charger.
2) Connect BMS output to the 5V boost.
3) Verify stable 5.0V with a multimeter.
4) Do NOT connect LED strips or MCU yet.

### Step 2: MCU and GNSS
1) Power the XIAO ESP32-S3 at 3.3V.
2) Connect GNSS:
   - GPS TX -> GPIO44 (D7) on MCU
   - GPS RX -> GPIO43 (D6) on MCU (optional)
   - Common GND
   - VCC per module (3.3V if applicable)
3) Add 10-47 uF + 0.1 uF between GNSS VCC and GND, as close to the module as possible.
4) If you power GNSS from the XIAO 3V3 rail, verify current margin; a dedicated LDO is ideal if noise appears.
5) Keep GNSS wiring short and away from the boost and LED 5V runs; avoid large loops.

### Step 3: Level shifting and LEDs
1) Power the 74AHCT125 with 5V and GND (if used).
2) Connect data lines by strip count:
    - Two strips:
     - D0 / GPIO1 -> IN1 -> OUT1 -> 330-470R (near DIN) -> LED A DIN
     - D1 / GPIO2 -> IN2 -> OUT2 -> 330-470R (near DIN) -> LED B DIN
    - Single strip:
     - D0 / GPIO1 -> IN1 -> OUT1 -> 330-470R (near DIN) -> LED A DIN
     - Do not use D1 / GPIO2
3) If NO level shifter (prototype only): connect D0/D1 (GPIO1/GPIO2) directly to DIN with a 330-470 ohm series resistor (near DIN) and keep wires short.
4) Connect LED strips VDD/GND to 5V/GND.
5) Place a 1000 uF capacitor on 5V near the first LED (ideally one per strip).
6) Run 5V and GND to each strip in parallel (not in series); use AWG 22-24 for 5V/GND.
7) If you see voltage drop or long runs, inject 5V/GND at the end of each strip.

### Step 4: Status LED (optional)
1) GPIO3 -> resistor -> LED -> GND.

---

## 6.1) Notes on 4-wire LED strips

SK6812 strips usually have 4 wires: +5V, GND, DIN, and DOUT (or DI/DO).

- Single strip: connect +5V, GND, and DIN. DOUT is not needed.
- Chained strips: connect DOUT of the first to DIN of the second.
- Two separate strips (this project): each strip uses its own DIN (D0/GPIO1 and D1/GPIO2).

---

## 6.2) No level shifter option (best practice)

If you skip level shifting, use these rules for stability:

- 330-470 ohm series resistor on each data line, as close to the MCU as possible.
- Keep data wires short (ideal <10-15 cm).
- Common GND with a direct return to the same power point.
- Start with low brightness (30% or less).
- If possible, reduce LED VDD to ~4.0-4.5 V for better 3.3V logic margin.

---

## 6.3) Wiring best practices (GPS + LEDs)

- Use a star GND point at the boost output, then branch to MCU, GNSS, and strips.
- Keep GNSS and its antenna away from the boost and LED 5V wiring (min 3-5 cm if possible).
- If cables are long, twist GNSS TX+GND and avoid running them parallel to LED 5V.
- For long strips or voltage drop, inject 5V/GND at both ends and add 1000 uF per strip.
- Secure cables to avoid flex fatigue (internal strain relief).

---

## 7) Bench tests (before closing)

1) Continuity: confirm common GND and no 5V-GND shorts.
2) Power only MCU and GNSS: confirm GPS output on serial.
3) Power LEDs at low brightness: confirm both strips light.
4) Verify Wi-Fi portal (AP and STA) and `/config` route.
5) For daily use and configuration, see the user manual: [docs/manual_de_uso.md](docs/manual_de_uso.md).

---

## 8) Download and flash the ESP32

This firmware uses PlatformIO.

1) Install PlatformIO (VS Code or CLI).
2) Open `Platformio/Dog-RGB/` as a project.
3) Connect the XIAO ESP32-S3 via USB.
4) Build:
   - `pio run -e esp32s3`
5) Upload:
   - `pio run -e esp32s3 -t upload`
6) Serial monitor:
   - `pio device monitor -e esp32s3`

If the port does not appear, check the USB cable and drivers.

---

## 9) Final assembly

1) Secure connections with heat-shrink.
2) Fix LED strip inside diffuser with no cable strain.
3) Install enclosure with strain relief.
4) Ensure no sharp points or rigid pressure spots.

---

## 10) Maintenance and tuning

- Recharge via USB-C charger.
- Wipe clean with a damp cloth (do not submerge unless IP67).
- Update pins in `Platformio/Dog-RGB/include/pins.h` if wiring changes.
- Update parameters in `Platformio/Dog-RGB/include/config.h` if LED count changes.

---

## 11) Common mistakes and prevention

- LEDs not lighting: check 5V, common GND, and DIN direction.
- GPS no fix: test outdoors and verify TX/RX crossed.
- MCU resets: check 5V sag and ground integrity.
- LED flicker: use level shifter and series resistor.
