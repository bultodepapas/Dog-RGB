# XIAO ESP32-S3 Pin Map

**Status:** Current project reference. Board capabilities should still be verified against the exact Seeed Studio board revision and its official pinout before wiring.

## Dog-RGB assignments

| XIAO label | ESP32-S3 GPIO | Dog-RGB use | Direction |
| --- | ---: | --- | --- |
| D0 | 1 | SK6812 strip A data | Output |
| D1 | 2 | SK6812 strip B data | Output |
| D2 | 3 | Optional external status LED | Output |
| D6 | 43 | GNSS RX input (module command line, optional) | Output from MCU |
| D7 | 44 | GNSS TX input (NMEA stream) | Input to MCU |

The default firmware uses two independent LED data buses. Each strip receives regulated 5 V power separately and shares ground with the controller and level shifter. See [SK6812 wiring](docs/sk6812_wiring.md).

## Header reference

| XIAO label | Primary function | GPIO | Common alternate functions |
| --- | --- | ---: | --- |
| D0 | Analog/GPIO | 1 | ADC, touch |
| D1 | Analog/GPIO | 2 | ADC, touch |
| D2 | Analog/GPIO | 3 | ADC, touch |
| D3 | Analog/GPIO | 4 | ADC, touch |
| D4 | SDA | 5 | I2C, ADC, touch |
| D5 | SCL | 6 | I2C, ADC, touch |
| D6 | TX | 43 | UART TX |
| D7 | RX | 44 | UART RX |
| D8 | SCK | 7 | SPI, ADC, touch |
| D9 | MISO | 8 | SPI, ADC, touch |
| D10 | MOSI | 10 | SPI, ADC, touch |
| D11 | GPIO | 42 | JTAG/ADC; shared with onboard microphone clock on Sense variants |
| D12 | GPIO | 41 | JTAG/ADC; shared with onboard microphone data on Sense variants |

## Power and control

| Label | Function | Note |
| --- | --- | --- |
| 5V | VBUS/5 V rail | Follow the board vendor's rules for input/output use and USB coexistence |
| 3V3 | Regulated 3.3 V output | Do not exceed the regulator's available current |
| GND | Ground | Must be common with GNSS, LED supply, and level shifter |
| RST/EN | Reset/enable | Board reset |
| BOOT | GPIO0 boot control | Used with reset for download/boot recovery |

## Shared-pin cautions

- GPIO41/GPIO42 may be occupied by the onboard microphone on XIAO ESP32-S3 Sense hardware.
- GPIO3/GPIO7/GPIO8/GPIO10 may be shared with the onboard microSD interface on Sense hardware.
- JTAG and onboard peripherals can conflict with alternate header use.
- Pin aliases in Arduino/PlatformIO are board-definition dependent; Dog-RGB stores the actual assignments in `Platformio/Dog-RGB/include/pins.h`.

When this page and the installed board definition disagree, verify the board revision and firmware source before applying power.
