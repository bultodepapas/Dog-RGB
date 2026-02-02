#ifndef DOG_RGB_PINS_H
#define DOG_RGB_PINS_H

// Adjust these pins to match the ESP32-S3 board and wiring.
static const int PIN_STATUS_LED = 3; // XIAO ESP32-S3 D2 / GPIO3 (external LED)
// SK6812 strip A (single-wire data) - Mapped to D0 / GPIO1
static const int PIN_LED_A_DATA = 1;
// SK6812 strip B (single-wire data) - Mapped to D1 / GPIO2
static const int PIN_LED_B_DATA = 2;
// Move GPS UART to D6/D7 (GPIO43/44) to avoid SD/SPI pin overlap.
static const int PIN_GPS_RX = 44; // XIAO ESP32-S3 D7 / GPIO44 (GPS TX -> ESP RX)
static const int PIN_GPS_TX = 43; // XIAO ESP32-S3 D6 / GPIO43 (ESP TX -> GPS RX)

#endif
