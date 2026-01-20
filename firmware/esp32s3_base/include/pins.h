#ifndef DOG_RGB_PINS_H
#define DOG_RGB_PINS_H

// Adjust these pins to match the ESP32-S3 board and wiring.
static const int PIN_STATUS_LED = 3; // XIAO ESP32-S3 D2 / GPIO3 (external LED)
static const int PIN_LED_DATA = 11;
static const int PIN_LED_CLOCK = 12;
static const int PIN_GPS_RX = 7; // XIAO ESP32-S3 D6 / GPIO7 (GPS TX -> ESP RX)
static const int PIN_GPS_TX = 8; // XIAO ESP32-S3 D7 / GPIO8 (ESP TX -> GPS RX)

#endif
