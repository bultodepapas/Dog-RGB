# Build Manual - Smart LED Dog Collar (MVP)

[Espanol](manual_de_construccion.es.md) | [English](manual_de_construccion.en.md)

Practical guide to build the hardware prototype.

---

## 1) Scope and warnings

- This manual covers the Phase 1 MVP (GPS + Wi-Fi portal + SK6812 LEDs).
- Basic electronics and soldering skills required.
- Test everything on the bench before sealing the enclosure.

---

## 2) Bill of materials (Base BOM)

### Main electronics
- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz, BDS/GPS/GLONASS)
- LEDs: SK6812 (5V, single-wire), 2 strips (20 LEDs each)
- Battery: 21700 Li-ion (~5000 mAh)
- BMS 1S + USB-C charger
- 5V boost >= 3A continuous
- 3.3V regulator (if not provided by the MCU)

### Level shifting and protection
- 5V level shifter: 74AHCT125 (or equivalent)
- Series resistors: 330-470 ohm (2 units, one per data line)
- Capacitor: 1000 uF electrolytic (5V rail, near first LED)
- Capacitor: 0.1 uF ceramic (near first LED, optional)

### Mechanical
- Nylon strap (20-25 mm width)
- Silicone diffuser tube
- IP65-IP67 enclosure with strain relief
- Silicone wire (AWG 22-24 for 5V/LEDs, AWG 26-28 for signals)

### Extras
- External status LED + resistor (if used)
- Physical switch or Hall sensor (optional)
- Heat-shrink, double-sided tape, epoxy adhesive

---

## 3) Tools

- Soldering iron and solder
- Multimeter
- Tweezers and cutters
- Heat gun (heat-shrink)
- Bench power supply (optional, recommended)

---

## 4) Wiring diagram (reference)

See the diagram in `docs/manual_de_uso.md`.

---

## 5) Preparation

1) Cut the strap and diffuser to length.
2) Prepare power and data wires.
3) Label pins and cables to avoid mistakes.

---

## 6) Step-by-step assembly

### Step 1: Power
1) Connect battery to the BMS.
2) Connect the BMS to the USB-C charger.
3) Connect the 5V boost to the BMS output.
4) Verify 5.0V output with a multimeter.

### Step 2: MCU and GNSS
1) Power the XIAO ESP32-S3 (3.3V).
2) Connect GNSS:
   - GPS TX -> GPIO7 (D6) on MCU
   - GPS RX -> GPIO8 (D7) on MCU (optional)
   - Common GND
   - VCC per module (3.3V if applicable)

### Step 3: Level shifting and LEDs
1) Power the 74AHCT125 with 5V and GND.
2) Connect data lines:
   - GPIO11 -> IN1 -> OUT1 -> 330-470R -> LED A DIN
   - GPIO12 -> IN2 -> OUT2 -> 330-470R -> LED B DIN
3) Connect LED strips VDD/GND to 5V/GND.
4) Place 1000 uF capacitor on 5V near the first LED.

### Step 4: Status LED (optional)
1) GPIO3 -> resistor -> LED -> GND.

---

## 7) Basic tests before closing

1) Check continuity and absence of shorts.
2) Power on and confirm MCU boot.
3) Verify GPS response and fix.
4) Verify LEDs light and change with speed.
5) Verify Wi-Fi portal (AP and STA).

---

## 8) Final assembly

1) Secure connections with heat-shrink.
2) Install enclosure with strain relief.
3) Fix LED strip inside diffuser.
4) Ensure no sharp points or rigid pressure spots.

---

## 9) Maintenance

- Recharge via USB-C charger.
- Wipe clean with a damp cloth.
- Inspect cables and connectors periodically.

---

## 10) Final notes

- Update pins in `firmware/esp32s3_base/include/pins.h` if wiring changes.
- Update parameters in `firmware/esp32s3_base/include/config.h` if LED count changes.
