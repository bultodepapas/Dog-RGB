# SK6812 Wiring Guide (ESP32-S3 / XIAO)

This guide covers safe wiring for SK6812 strips at 5V with a 3.3V MCU.

---

## Recommended Wiring

- 5V supply -> SK6812 VDD
- GND (common) -> SK6812 GND
- ESP32-S3 GPIO -> level shifter -> SK6812 DIN

---

## Level Shifting (Required)

- Use a 74AHCT125/74HCT125 or similar.
- Power the shifter at 5V.
- This ensures clean 5V data for SK6812.

---

## Decoupling

- Place 1000 uF electrolytic across 5V and GND near the strip input.
- Add 0.1 uF ceramic near the first LED if possible.

---

## Data Line Protection

- Add a 330-470 ohm resistor in series with DIN.

---

## Power Notes

- Keep brightness limited (~30%) for battery life.
- Ensure 5V boost can handle peak current.

---

## Pins (XIAO ESP32-S3)

- LED A data: GPIO11
- LED B data: GPIO12

Adjust in `firmware/esp32s3_base/include/pins.h` if needed.
