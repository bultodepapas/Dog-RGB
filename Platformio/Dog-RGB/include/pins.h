#ifndef DOG_RGB_PINS_H
#define DOG_RGB_PINS_H

#include "board/board_profile.h"

// Compatibility facade for the shared GPS and LED modules. GPIO selection
// must be available to global constructors, before setup() runs.
static const int PIN_STATUS_LED = board::kStatusLedPin;
static const int PIN_LED_A_DATA = board::kLedAData;
static const int PIN_LED_B_DATA = board::kLedBData;
static const int PIN_GPS_RX = board::kGpsRx;
static const int PIN_GPS_TX = board::kGpsTx;

#if defined(DOG_RGB_WOKWI_SIM)
// Wokwi-only UART0 console routing. The physical build continues to use USB
// Serial/JTAG, while simulation keeps D6/D7 exclusively assigned to GNSS.
static const int PIN_WOKWI_SERIAL_RX = 8; // XIAO D9 / GPIO8
static const int PIN_WOKWI_SERIAL_TX = 9; // XIAO D10 / GPIO9
#endif

#endif
