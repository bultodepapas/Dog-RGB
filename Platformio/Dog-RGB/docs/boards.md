# Board profiles and Waveshare I0 bring-up

Status: **I0 implemented in software, physical acceptance pending**, 2026-09-12.
Classic remains the working baseline. Waveshare is a **No Touch V2 candidate**;
neither a successful build nor its manifest identifies the PCB on your desk.
The [incremental plan](../../../docs/PLANS/2026-09-12_display-incremental-delivery.md)
governs subsequent LED, GPS and display work.

## Targets and ownership

| Environment | Board profile | Current behavior |
| --- | --- | --- |
| `seeed_xiao_esp32s3` | `DOG_RGB_BOARD_XIAO_S3=1` | Existing Classic application and defaults; default build target |
| `wokwi` | Classic, with existing simulation flags | Existing Classic UART routing and LED transport cadence |
| `waveshare_lcd169` | `DOG_RGB_BOARD_WAVESHARE_LCD169_V2=1` | Shared application with candidate pins; LCD disabled, physical integration unverified |
| `waveshare_lcd169_bringup` | Waveshare, `DOG_RGB_BRINGUP_STAGE=0` | Power retention, USB and periodic board/memory diagnostics only |

The application has one `main.cpp`. `include/pins.h` remains the facade used by
the GPS/LED modules, with compile-time selection before global constructors.
Missing/multiple profiles, Wokwi on Waveshare, and unimplemented diagnostic
stages fail compilation. Stage 1+ is deliberately not accepted yet.

Product source filters exclude `bringup/` and `display/`. I0 bringup includes
only `main.cpp`, `board/` and `bringup/`; its setup/loop do not start storage,
scenes, GPS, LEDs, BLE or Wi-Fi. The product Waveshare target still follows the
normal application boot, including welcome; use **bringup** for the I0 bench
check, with external strips/GNSS disconnected. No LCD or LVGL library was added.

## Candidate electrical mapping

| Signal | Classic GPIO | Waveshare V2 candidate GPIO |
| --- | ---: | ---: |
| Strip A data | 1 | 17 |
| Strip B data | 2 | 18 |
| GNSS TX → ESP RX | 44 | 44 |
| ESP TX → GNSS RX | 43 | 43 |
| External heartbeat | 3 | None (`-1`, never passed to GPIO API) |
| Power hold SYS_EN | None | 41, HIGH |
| Power button observation SYS_OUT | None | 40, input |
| LCD backlight | None | 15, LOW in I0 |

`board::begin()` runs before Serial and the normal application. On Waveshare
it asserts power hold, configures SYS_OUT as an input and disables backlight.
It does not assign navigation or shutdown semantics to the button, touch the
external LED data pins, or transmit a black strip frame. Previously powered
pixels can retain their previous color; “external_io=inactive” means this
diagnostic does not drive them, not that it measured them powered off.

The profiles reserve USB, flash/OPI PSRAM and, on Waveshare, onboard peripheral
pins; external collar pins must be valid, distinct and outside that set.
GPIO1 is the Waveshare battery ADC, so the Classic binary is not a wiring or
flashing substitute. Always confirm connector continuity, not wire colors.

## Manifest provenance and limits

The local [`waveshare_lcd169_v2.json`](../boards/waveshare_lcd169_v2.json) is
project-authored configuration using the pinned pioarduino ESP32-S3 manifest
schema and generic `esp32s3` Arduino variant. It explicitly selects 16 MiB flash,
QIO flash/OPI PSRAM (`qio_opi`), 240 MHz CPU, 80 MHz flash and USB CDC on boot.
It does not inherit the XIAO USB identity or variant. Generic Arduino SPI/I2C
default pins are not Waveshare pins: future drivers must pass profile pins
explicitly. No SPI/I2C peripheral is initialized in I0.

Hardware sources consulted:

- [Waveshare No Touch hardware documentation](https://docs.waveshare.net/ESP32-S3-LCD-1.69/): ESP32-S3R8, 8 MiB PSRAM and external 16 MiB flash.
- [Official V2 schematic](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.69/ESP32-S3-LCD-1.69_V2.pdf): integrated GPIOs and external connector.
- [No Touch wiki](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.69) and [Arduino guide](https://docs.waveshare.net/ESP32-S3-LCD-1.69/Arduino/): SYS_EN power retention and revision-dependent GPIO35 versus GPIO41. Only the V2 candidate is configured here.

These are configuration evidence, not physical measurements. The platform,
ArduinoJson and NeoPixel versions remain unchanged. All targets keep
`partitions_dog_rgb.csv`, including `tracknvs`, in the first 8 MiB. Application
capacity is still 3,342,336 bytes per slot; 16 MiB flash does not enlarge it or
implement OTA. The existing estimated-current defaults have not been calibrated
for Waveshare. Battery charging and portable mounting remain separate gates.

## Build and bench procedure

From `Platformio/Dog-RGB`:

```powershell
pio run -e seeed_xiao_esp32s3 -e wokwi
pio run -e waveshare_lcd169 -e waveshare_lcd169_bringup
python -m unittest discover -s test -p "test_*.py" -v
```

If PlatformIO is installed by VS Code but absent from PATH, its local executable
can be invoked explicitly, for example:

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe" run -e waveshare_lcd169_bringup
```

Before physical upload, identify the actual model/revision, match its schematic
and power-hold circuit, and identify its serial port. I0 uses a known USB supply
with battery and external collar wiring disconnected. Flash the complete
**bringup** target to the identified device, preserving the intended partition
layout; do not flash all environments or erase NVS as a workaround.

1. Attach the USB monitor at 115200 using the bringup environment. No firmware
   wait for a host connection is required; reports repeat once a second, so a
   late monitor still receives identity and reset information.
2. Expect `board=display-waveshare-lcd169 revision=v2-candidate stage=0`,
   `flash=16777216/16777216`, `psram=8388608/8388608` and `lcd=off`.
   Values before `/` are reported by the ESP APIs; values after it are the
   configured expectations. A zero/mismatched PSRAM result is an open failure,
   not a passed memory check. This is capacity detection, not an exhaustive RAM test.
3. Record reset reason, uptime, heap/minimum heap and raw `sys_out` before/after
   button presses. The code does not interpret those presses or cut power.
4. Perform five requested resets and ten minutes of observation. Record any
   unsolicited reset, USB loss or memory mismatch. Confirm that the blank LCD
   is expected. There is no panel-presence test in I0.
5. Save results with board identification and supply conditions in the
   [I0 baseline](../../../docs/baselines/display-i0-2026-09-12.md). Battery power
   retention/cutoff requires a separately verified battery connection; a USB-only
   run cannot prove that path. Proceed to LED bench work after I0 physical acceptance.

No physical upload or bench measurement was performed in this implementation.
