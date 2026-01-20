#ifndef DOG_RGB_CONFIG_H
#define DOG_RGB_CONFIG_H

// Speed-to-color ranges for Segment B (km/h).
// Adjust these first to tune activity colors.
static const float SPEED_RANGE_1_KPH = 1.5f;
static const float SPEED_RANGE_2_KPH = 4.0f;
static const float SPEED_RANGE_3_KPH = 7.0f;

// Motion filters and activity thresholds.
static const float SPEED_ACTIVE_KPH = 0.7f; // Min speed to count as "active".
static const float SPEED_MAX_VALID_KPH = 40.0f; // Reject GPS spikes above this.

// LED hardware (strip size and layout).
// These are common to change per collar size.
static const int LED_STRIP_MODE = 2; // 1 = single strip, 2 = dual strips.
static const int LED_STRIP_COUNT = 20; // LEDs per strip (min 10, max 50).
static const int LED_STATUS_COUNT = 3; // First N LEDs reserved for status.
static const uint8_t LED_BRIGHTNESS = 77; // ~30% brightness (0-255).

// LED UI timing.
static const unsigned long LED_UPDATE_MS = 50; // Refresh interval for LED UI.
static const unsigned long CRITICAL_NO_OK_MS = 600000; // Error if no GPS/Wi-Fi for this long.
static const bool LED_UI_ENABLED = true; // Disable to turn off LED UI logic.

// Wi-Fi settings (less common to change).
static const char *AP_SSID = "dog"; // AP name for direct connection.
static const char *AP_PASS = "Dog123456789"; // AP password (>= 8 chars).
static const char *MDNS_NAME = "dog-collar"; // mDNS hostname in STA mode.
static const unsigned long STA_CONNECT_TIMEOUT_MS = 10000; // STA connect timeout.
static const unsigned long WIFI_RETRY_INTERVAL_MS = 10000; // Watchdog retry interval.

// GNSS settings (rare changes).
static const uint32_t GPS_BAUD = 9600; // GNSS UART baudrate.
static const unsigned long GPS_SAMPLE_MS = 1000; // Sampling interval.

// Persistence (rare changes).
static const unsigned long SAVE_INTERVAL_MS = 60000; // NVS save interval.

#endif
