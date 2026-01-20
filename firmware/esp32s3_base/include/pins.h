#ifndef DOG_RGB_PINS_H
#define DOG_RGB_PINS_H

// Adjust these pins to match the ESP32-S3 board and wiring.
static const int PIN_STATUS_LED = 3; // XIAO ESP32-S3 D2 / GPIO3 (external LED)
// SK6812 strip A (single-wire data) - Mapped to GPIO11
static const int PIN_LED_A_DATA = 11;
// SK6812 strip B (single-wire data) - Mapped to GPIO12
static const int PIN_LED_B_DATA = 12;
static const int PIN_GPS_RX = 7; // XIAO ESP32-S3 D6 / GPIO7 (GPS TX -> ESP RX)
static const int PIN_GPS_TX = 8; // XIAO ESP32-S3 D7 / GPIO8 (ESP TX -> GPS RX)

#endif
