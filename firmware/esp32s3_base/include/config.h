#ifndef DOG_RGB_CONFIG_H
#define DOG_RGB_CONFIG_H

// LED hardware
static const int LED_STRIP_MODE = 2; // 1 = single strip, 2 = dual strips.
static const int LED_STRIP_COUNT = 20; // LEDs per strip (min 10, max 50).
static const int LED_STATUS_COUNT = 3; // First N LEDs reserved for status.
static const uint8_t LED_BRIGHTNESS = 77; // ~30% brightness (0-255).

// LED UI timing
static const unsigned long LED_UPDATE_MS = 50;
static const unsigned long CRITICAL_NO_OK_MS = 600000;

// Speed-to-color ranges for Segment B (km/h).
static const float SPEED_RANGE_1_KPH = 1.5f;
static const float SPEED_RANGE_2_KPH = 4.0f;
static const float SPEED_RANGE_3_KPH = 7.0f;

// Wi-Fi settings
static const char *AP_SSID = "dog";
static const char *AP_PASS = "Dog123456789";
static const char *MDNS_NAME = "dog-collar";
static const unsigned long STA_CONNECT_TIMEOUT_MS = 10000;
static const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

// GNSS settings
static const uint32_t GPS_BAUD = 9600;
static const unsigned long GPS_SAMPLE_MS = 1000;
static const float SPEED_ACTIVE_KPH = 0.7f;
static const float SPEED_MAX_VALID_KPH = 40.0f;

// Persistence
static const unsigned long SAVE_INTERVAL_MS = 60000;

// LED UI enable
static const bool LED_UI_ENABLED = true;

#endif
