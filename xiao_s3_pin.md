# XIAO ESP32-S3 Pin Map (Reference)

This file is a clean, formatted reference based on the official pinout details.

---

## Power / Control

| XIAO Pin | Function | Chip Pin | Alternate Functions | Description |
| --- | --- | --- | --- | --- |
| 5V | VBUS |  |  | Power Input/Output |
| GND |  |  |  | Ground |
| 3V3 | 3V3_OUT |  |  | Power Output |
| Reset | EN | EN |  | Reset/Enable |
| Boot |  | GPIO0 |  | Enter Boot Mode |

---

## Header Pins (D0-D12)

| XIAO Pin | Function | Chip Pin | Alternate Functions | Description |
| --- | --- | --- | --- | --- |
| D0 | Analog | GPIO1 | TOUCH1 | GPIO, ADC |
| D1 | Analog | GPIO2 | TOUCH2 | GPIO, ADC |
| D2 | Analog | GPIO3 | TOUCH3 | GPIO, ADC |
| D3 | Analog | GPIO4 | TOUCH4 | GPIO, ADC |
| D4 | Analog, SDA | GPIO5 | TOUCH5 | GPIO, I2C Data, ADC |
| D5 | Analog, SCL | GPIO6 | TOUCH6 | GPIO, I2C Clock, ADC |
| D6 | TX | GPIO43 |  | GPIO, UART Transmit |
| D7 | RX | GPIO44 |  | GPIO, UART Receive |
| D8 | Analog, SCK | GPIO7 | TOUCH7 | GPIO, SPI Clock, ADC |
| D9 | Analog, MISO | GPIO8 | TOUCH8 | GPIO, SPI Data, ADC |
| D10 | Analog, MOSI | GPIO10 | TOUCH9 | GPIO, SPI Data, ADC |
| D11 | Analog | GPIO42 | TOUCH12 | GPIO, ADC |
| D12 | Analog | GPIO41 | TOUCH13 | GPIO, ADC |

---

## JTAG / Debug

| XIAO Pin | Function | Chip Pin | Alternate Functions | Description |
| --- | --- | --- | --- | --- |
| MTDO |  | GPIO40 |  | JTAG |
| MTDI |  | GPIO41 |  | JTAG, ADC |
| MTCK |  | GPIO39 |  | JTAG, ADC |
| MTMS |  | GPIO42 |  | JTAG, ADC |

---

## Onboard Peripherals

| Peripheral | XIAO Pin | Chip Pin | Description |
| --- | --- | --- | --- |
| U.FL-R-SMT1 |  | LNA_IN | UFL antenna |
| CHARGE_LED |  |  | CHG-LED |
| USER_LED |  | GPIO21 | User Light |
| Digital microphone_CLK |  | GPIO42 | PDM clock pin for MIC |
| Digital microphone_DATA |  | GPIO41 | PDM data pin for MIC |
| Onboard SD Card__CS |  | GPIO3 | SD card chip select pin |
| Onboard SD Card_SCK |  | GPIO7 | SD card clock pin |
| Onboard SD Card_MISO |  | GPIO8 | SD card data input pin |
| Onboard SD Card Slot_MOSI |  | GPIO10 | SD card data output pin |

---

## Notes (Pin Sharing)

- GPIO41 and GPIO42 appear in both D12/D11 and JTAG/MIC functions.
- GPIO3, GPIO7, GPIO8, and GPIO10 appear in D2/D8/D9/D10 and the onboard SD card signals.
