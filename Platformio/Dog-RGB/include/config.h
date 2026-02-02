#ifndef DOG_RGB_CONFIG_H
#define DOG_RGB_CONFIG_H

// Speed-to-color ranges for Segment B (km/h).
// Adjust these first to tune activity colors (10 ranges / 9 thresholds).
static const float SPEED_RANGE_1_KPH = 2.0f;
static const float SPEED_RANGE_2_KPH = 4.0f;
static const float SPEED_RANGE_3_KPH = 6.0f;
static const float SPEED_RANGE_4_KPH = 8.0f;
static const float SPEED_RANGE_5_KPH = 12.0f;
static const float SPEED_RANGE_6_KPH = 16.0f;
static const float SPEED_RANGE_7_KPH = 22.0f;
static const float SPEED_RANGE_8_KPH = 28.0f;
static const float SPEED_RANGE_9_KPH = 34.0f;

// Effect selection for Segment B.
// 0=SOLID, 1=PULSE, 2=BREATH, 3=CHASE, 4=COMET, 5=SINELON,
// 6=CONFETTI, 7=JUGGLE, 8=BPM, 9=RAINBOW, 10=FIRE, 11=GRADIENT_WAVE
static const int RANGE_1_EFFECT_A = 0;
static const int RANGE_1_EFFECT_B = 0;
static const int RANGE_2_EFFECT_A = 1;
static const int RANGE_2_EFFECT_B = 1;
static const int RANGE_3_EFFECT_A = 2;
static const int RANGE_3_EFFECT_B = 2;
static const int RANGE_4_EFFECT_A = 3;
static const int RANGE_4_EFFECT_B = 3;
static const int RANGE_5_EFFECT_A = 5;
static const int RANGE_5_EFFECT_B = 5;
static const int RANGE_6_EFFECT_A = 7;
static const int RANGE_6_EFFECT_B = 7;
static const int RANGE_7_EFFECT_A = 8;
static const int RANGE_7_EFFECT_B = 8;
static const int RANGE_8_EFFECT_A = 9;
static const int RANGE_8_EFFECT_B = 9;
static const int RANGE_9_EFFECT_A = 11;
static const int RANGE_9_EFFECT_B = 11;
static const int RANGE_10_EFFECT_A = 10;
static const int RANGE_10_EFFECT_B = 10;

// Effect tuning per range (0-255).
static const uint8_t RANGE_1_SPEED = 40;
static const uint8_t RANGE_1_INTENSITY = 80;
static const uint8_t RANGE_2_SPEED = 58;
static const uint8_t RANGE_2_INTENSITY = 95;
static const uint8_t RANGE_3_SPEED = 76;
static const uint8_t RANGE_3_INTENSITY = 110;
static const uint8_t RANGE_4_SPEED = 94;
static const uint8_t RANGE_4_INTENSITY = 125;
static const uint8_t RANGE_5_SPEED = 112;
static const uint8_t RANGE_5_INTENSITY = 140;
static const uint8_t RANGE_6_SPEED = 130;
static const uint8_t RANGE_6_INTENSITY = 155;
static const uint8_t RANGE_7_SPEED = 148;
static const uint8_t RANGE_7_INTENSITY = 170;
static const uint8_t RANGE_8_SPEED = 166;
static const uint8_t RANGE_8_INTENSITY = 180;
static const uint8_t RANGE_9_SPEED = 184;
static const uint8_t RANGE_9_INTENSITY = 190;
static const uint8_t RANGE_10_SPEED = 200;
static const uint8_t RANGE_10_INTENSITY = 200;

// Motion filters and activity thresholds.
static const float SPEED_ACTIVE_KPH = 0.7f; // Min speed to count as "active".
static const float SPEED_MAX_VALID_KPH = 40.0f; // Reject GPS spikes above this.

// LED hardware (strip size and layout).
// These are common to change per collar size.
static const int LED_STRIP_MODE = 2; // 1 = single strip, 2 = dual strips.
static const int LED_STRIP_COUNT = 24; // LEDs per strip (min 10, max 50).
static const int LED_STATUS_COUNT = 2; // First N LEDs reserved for status.
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
static const unsigned long AP_IDLE_TIMEOUT_MS = 300000; // AP off if no clients for this long.
static const unsigned long AP_STATIONARY_MS = 120000; // AP on if speed <= threshold for this long.
static const unsigned long AP_CLIENT_POLL_MS = 1000; // Station count polling interval.
static const float AP_STATIONARY_ON_KPH = 2.0f; // Enter stationary when <= this speed.
static const float AP_STATIONARY_OFF_KPH = 2.5f; // Exit stationary when >= this speed.
static const unsigned long WIFI_OFF_GPS_FIX_MS = 300000; // Homogeneous LED mode after GPS fix stable.
static const unsigned long AP_OFF_PULSE_PERIOD_MS = 3000; // AP off double-pulse period.
static const unsigned long AP_OFF_PULSE_MS = 200; // AP off pulse width.

// GNSS settings (rare changes).
static const uint32_t GPS_BAUD = 9600; // GNSS UART baudrate.
static const unsigned long GPS_SAMPLE_MS = 1000; // Sampling interval.

// Persistence (rare changes).
static const unsigned long SAVE_INTERVAL_MS = 60000; // NVS save interval.

#endif
