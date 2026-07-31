#ifndef DOG_RGB_CONFIG_H
#define DOG_RGB_CONFIG_H

// Speed-to-color ranges for Segment B (km/h).
// Adjust these first to tune activity colors (10 ranges / 9 thresholds).
static const float SPEED_RANGE_1_KPH = 2.0f;
static const float SPEED_RANGE_2_KPH = 4.0f;
static const float SPEED_RANGE_3_KPH = 6.0f;
static const float SPEED_RANGE_4_KPH = 8.0f;
static const float SPEED_RANGE_5_KPH = 10.0f;
static const float SPEED_RANGE_6_KPH = 12.0f;
static const float SPEED_RANGE_7_KPH = 14.0f;
static const float SPEED_RANGE_8_KPH = 16.0f;
static const float SPEED_RANGE_9_KPH = 18.0f;

// Effect selection for Segment B.
// 0=SOLID, 1=PULSE, 2=BREATH, 3=CHASE, 4=COMET, 5=SINELON,
// 6=CONFETTI, 7=JUGGLE, 8=BPM, 9=RAINBOW, 10=FIRE, 11=GRADIENT_WAVE
static const int RANGE_1_EFFECT_A = 7;
static const int RANGE_1_EFFECT_B = 7;
static const int RANGE_2_EFFECT_A = 7;
static const int RANGE_2_EFFECT_B = 7;
static const int RANGE_3_EFFECT_A = 7;
static const int RANGE_3_EFFECT_B = 7;
static const int RANGE_4_EFFECT_A = 7;
static const int RANGE_4_EFFECT_B = 7;
static const int RANGE_5_EFFECT_A = 7;
static const int RANGE_5_EFFECT_B = 7;
static const int RANGE_6_EFFECT_A = 7;
static const int RANGE_6_EFFECT_B = 7;
static const int RANGE_7_EFFECT_A = 7;
static const int RANGE_7_EFFECT_B = 7;
static const int RANGE_8_EFFECT_A = 7;
static const int RANGE_8_EFFECT_B = 7;
static const int RANGE_9_EFFECT_A = 7;
static const int RANGE_9_EFFECT_B = 7;
static const int RANGE_10_EFFECT_A = 7;
static const int RANGE_10_EFFECT_B = 7;

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

// Mode defaults and geofence tuning.
static const uint8_t MODE_SPEED = 0;
static const uint8_t MODE_GEOFENCE = 1;
static const uint8_t MODE_SHOW = 2;
static const uint8_t MODE_SIMPLE = 3;
static const uint16_t GEOFENCE_MAX_M_DEFAULT = 300;
static const uint16_t GEOFENCE_MAX_M_MIN = 50;
static const uint16_t GEOFENCE_MAX_M_MAX = 5000;
static const unsigned long HOME_AUTO_FIX_MS = 10000; // Auto-home after GPS fix stable.
static const float GEOFENCE_HYSTERESIS_PCT = 0.03f;
static const float GEOFENCE_HYSTERESIS_MIN_M = 5.0f;

// LED hardware (strip size and layout).
// These are common to change per collar size.
static const int LED_STRIP_MODE = 2; // 1 = single strip, 2 = dual strips.
static const int LED_STRIP_COUNT = 24; // LEDs per strip (min 10, max 50).
static const int LED_STATUS_COUNT = 2; // First N LEDs reserved for status.
static const uint8_t LED_BRIGHTNESS = 77; // ~30% brightness (0-255).
static const bool LED_DEBUG_BRIGHTNESS_ENABLED = false; // Diagnostic: override runtime brightness.
static const uint8_t LED_DEBUG_BRIGHTNESS = 30; // Low-brightness debug value.
static const bool DEBUG_AP_ONLY_MINIMAL = false; // Diagnostic: boot only AP + portal, no GPS/LED/BLE/STA.

// LED UI timing.
static const unsigned long LED_UPDATE_MS = 50; // Refresh interval for LED UI.
static const unsigned long CRITICAL_NO_OK_MS = 600000; // Error if no GPS/Wi-Fi for this long.
static const bool LED_UI_ENABLED = true; // Disable to turn off LED UI logic.

// Day mode power saving. Times are local minutes since midnight.
static const uint16_t DAY_MODE_START_MIN = 6 * 60;
static const uint16_t DAY_MODE_END_MIN = 16 * 60;
static const int16_t DAY_MODE_TZ_OFFSET_MIN = -300; // America/Bogota (UTC-5).
static const unsigned long DAY_MODE_TIME_STALE_MS = 300000; // Require recent trusted GPS time.

// LED SHOW mode (demo).
static const uint8_t EFFECT_COUNT = 12; // IDs 0..11
static const unsigned long SHOW_EFFECT_MS = 30000;
static const uint8_t SHOW_SPEED = 150;
static const uint8_t SHOW_INTENSITY = 200;
static const uint8_t SINGLE_EFFECT_DEFAULT = 0; // SOLID
static const uint8_t SINGLE_SPEED_DEFAULT = 80;
static const uint8_t SINGLE_INTENSITY_DEFAULT = 140;
static const uint8_t SINGLE_R_DEFAULT = 0;
static const uint8_t SINGLE_G_DEFAULT = 60;
static const uint8_t SINGLE_B_DEFAULT = 60;

// Wi-Fi boot robustness (boot-time only, not hot-path).
static const uint32_t WIFI_BOOT_STABILIZE_MS    = 200;  // Delay after WiFi.mode(OFF) hard-reset in begin().
static const uint32_t WIFI_MODE_SETTLE_MS        = 150;  // Delay after WiFi.mode() before softAPConfig/softAP.
static const uint32_t WIFI_BLE_COEX_MS           = 150;  // Margin between BLE init and WiFi init.
static const int      WIFI_BOOT_AP_MAX_ATTEMPTS  = 3;    // Boot retry attempts if softAP returns false.
static const uint32_t WIFI_BOOT_AP_RETRY_DELAY_MS = 500; // Delay between boot retry attempts.

// Wi-Fi settings (less common to change).
static const char *AP_SSID = "DogRGB"; // AP name for direct connection.
static const char *AP_PASS = "Dog12345"; // AP password (>= 8 chars).
static const char *MDNS_NAME = "dog-collar"; // mDNS hostname in STA mode.
static const uint8_t AP_CHANNEL = 1; // SoftAP channel when STA is not connected.
static const uint8_t AP_MAX_CLIENTS = 2; // Keep setup surface small and predictable.
static const unsigned long STA_CONNECT_TIMEOUT_MS = 10000; // STA connect timeout.
static const unsigned long WIFI_RETRY_INTERVAL_MS = 10000; // Watchdog retry interval.
static const unsigned long STA_RETRY_BACKOFF_MAX_MS = 300000; // Max STA retry backoff.
static const unsigned long AP_SETUP_HOLD_MS = 900000; // Keep AP visible after AP start.
static const unsigned long AP_PORTAL_ACTIVITY_HOLD_MS = 300000; // Keep AP after portal traffic.
static const unsigned long AP_IDLE_TIMEOUT_MS = 600000; // AP off if no clients for this long.
static const unsigned long AP_STATIONARY_MS = 120000; // AP on if speed <= threshold for this long.
// AP events update the count immediately. This slower poll is only a fallback
// reconciliation for driver/backend drift and keeps synchronous radio queries
// from stalling the application loop every second.
static const unsigned long AP_CLIENT_POLL_MS = 60000;
static const float AP_STATIONARY_ON_KPH = 2.0f; // Enter stationary when <= this speed.
static const float AP_STATIONARY_OFF_KPH = 2.5f; // Exit stationary when >= this speed.
static const unsigned long WIFI_OFF_GPS_FIX_MS = 300000; // Homogeneous LED mode after GPS fix stable.
static const unsigned long AP_OFF_PULSE_PERIOD_MS = 3000; // AP off double-pulse period.
static const unsigned long AP_OFF_PULSE_MS = 200; // AP off pulse width.

// GNSS settings (rare changes).
static const uint32_t GPS_BAUD = 9600; // GNSS UART baudrate.
#if defined(DOG_RGB_WOKWI_SIM)
// The simulator uses a real timed UART for diagnostics. A faster console keeps
// rich logs without blocking the emulated MCU for ~170 ms every report.
static const uint32_t CONSOLE_BAUD = 460800;
#else
static const uint32_t CONSOLE_BAUD = 115200;
#endif
// Covers more than 17 seconds of worst-case 8N1 input at 9600 baud while the
// synchronous portal is sending a response to a slow Wi-Fi client.
static const uint16_t GPS_RX_BUFFER_SIZE = 16384;
static const unsigned long GPS_SAMPLE_MS = 1000; // Sampling interval.
// Only bridge active-time observations this far apart. Buffered 1 Hz RMC
// sentences remain individually countable after a loop stall; a lone fix
// after a longer outage cannot invent activity for the unobserved gap.
static const uint32_t GPS_ACTIVE_MAX_GAP_MS = 3000;
// A non-contiguous forward date must be repeated by trusted RMC observations
// before daily metrics roll. A real midnight with continuous timestamps can
// advance immediately; backward dates are never accepted automatically.
static const uint8_t GPS_DATE_CONFIRM_OBSERVATIONS = 3;
static const uint32_t GPS_DATE_CONFIRM_MAX_GAP_MS = 3000;
// GPS quality gating defaults and bounds.
static const uint8_t GPS_MIN_FIX_QUALITY_DEFAULT = 1;
static const uint8_t GPS_MIN_FIX_QUALITY_MIN = 0;
static const uint8_t GPS_MIN_FIX_QUALITY_MAX = 8;
static const uint8_t GPS_MIN_SATS_DEFAULT = 6;
static const uint8_t GPS_MIN_SATS_MIN = 3;
static const uint8_t GPS_MIN_SATS_MAX = 12;
static const float GPS_MAX_HDOP_DEFAULT = 2.5f;
static const float GPS_MAX_HDOP_MIN = 0.5f;
static const float GPS_MAX_HDOP_MAX = 20.0f;
static const uint16_t GPS_MAX_GGA_AGE_MS_DEFAULT = 2000;
static const uint16_t GPS_MAX_GGA_AGE_MS_MIN = 500;
static const uint16_t GPS_MAX_GGA_AGE_MS_MAX = 10000;
static const float GPS_MIN_SEGMENT_M_DEFAULT = 3.0f;
static const float GPS_MIN_SEGMENT_M_MIN = 0.5f;
static const float GPS_MIN_SEGMENT_M_MAX = 20.0f;
static const float GPS_HDOP_FACTOR_DEFAULT = 2.0f;
static const float GPS_HDOP_FACTOR_MIN = 0.0f;
static const float GPS_HDOP_FACTOR_MAX = 5.0f;
static const float GPS_MAX_MIN_SEGMENT_M_DEFAULT = 10.0f;
static const float GPS_MAX_MIN_SEGMENT_M_MIN = 1.0f;
static const float GPS_MAX_MIN_SEGMENT_M_MAX = 50.0f;

// Persistence (rare changes).
static const unsigned long SAVE_INTERVAL_MS = 60000; // NVS save interval.

// BLE summary feature.
// The XIAO ESP32-S3 has a single PCB antenna shared between WiFi and BLE.
// Espressif's coexistence table marks SoftAP + ANY BLE mode as C1 (unstable).
// Even without advertising, BLEDevice::init() starts the BT controller which
// permanently timeslices the radio with WiFi, causing beacon starvation and
// making the SoftAP SSID invisible to phones.
// Keep this false until the BLE feature is needed. Flip to true only when
// running in STA-only mode or when a proper btStop()/btStart() strategy is in place.
static const bool BLE_ENABLED = false;

#endif
