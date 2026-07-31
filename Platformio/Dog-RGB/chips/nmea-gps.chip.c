#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wokwi-api.h"

enum {
  PROFILE_MOVING = 0,
  PROFILE_STOPPED = 1,
  PROFILE_NO_FIX = 2,
  PROFILE_POOR_HDOP = 3,
  PROFILE_NO_SIGNAL = 4,
};

typedef struct {
  uart_dev_t uart;
  timer_t timer;
  uint32_t profile_attr;
  uint32_t speed_kph_attr;
  uint32_t last_profile;
  uint32_t seconds_of_day;
  int32_t latitude_offset_1e4_min;
  int8_t latitude_direction;
  bool uart_busy;
  uint8_t day;
  uint8_t month;
  uint8_t year;
  char tx_buffer[320];
} chip_state_t;

static bool is_leap_year(uint16_t full_year) {
  return ((full_year % 4U) == 0U && (full_year % 100U) != 0U) ||
         (full_year % 400U) == 0U;
}

static uint8_t days_in_month(uint8_t month, uint8_t year) {
  static const uint8_t DAYS[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  if (month < 1U || month > 12U) {
    return 31U;
  }
  if (month == 2U && is_leap_year((uint16_t)(2000U + year))) {
    return 29U;
  }
  return DAYS[month - 1U];
}

static void advance_clock(chip_state_t *chip) {
  chip->seconds_of_day++;
  if (chip->seconds_of_day < 86400U) {
    return;
  }
  chip->seconds_of_day = 0U;
  chip->day++;
  if (chip->day <= days_in_month(chip->month, chip->year)) {
    return;
  }
  chip->day = 1U;
  chip->month++;
  if (chip->month > 12U) {
    chip->month = 1U;
    chip->year = (uint8_t)((chip->year + 1U) % 100U);
  }
}

static void advance_position(chip_state_t *chip, uint32_t speed_kph) {
  if (speed_kph == 0U) {
    return;
  }

  // One 0.0001-minute latitude unit is about 0.185 m. This keeps the
  // simulated displacement consistent with the reported speed at 1 Hz.
  int32_t step = (int32_t)((speed_kph * 150U + 50U) / 100U);
  if (step < 1) {
    step = 1;
  }
  chip->latitude_offset_1e4_min += step * chip->latitude_direction;
  if (chip->latitude_offset_1e4_min >= 5000) {
    chip->latitude_offset_1e4_min = 5000;
    chip->latitude_direction = -1;
  } else if (chip->latitude_offset_1e4_min <= 0) {
    chip->latitude_offset_1e4_min = 0;
    chip->latitude_direction = 1;
  }
}

static bool append_sentence(char *output,
                            size_t output_size,
                            size_t *used,
                            const char *body) {
  uint8_t checksum = 0U;
  for (const char *p = body; *p != '\0'; ++p) {
    checksum ^= (uint8_t)*p;
  }

  if (*used >= output_size) {
    return false;
  }
  const int written = snprintf(output + *used, output_size - *used,
                               "$%s*%02X\r\n", body, checksum);
  if (written < 0 || (size_t)written >= output_size - *used) {
    return false;
  }
  *used += (size_t)written;
  return true;
}

static bool build_frame(chip_state_t *chip,
                        uint32_t profile,
                        uint32_t speed_kph,
                        size_t *frame_length) {
  const uint32_t hour = chip->seconds_of_day / 3600U;
  const uint32_t minute = (chip->seconds_of_day / 60U) % 60U;
  const uint32_t second = chip->seconds_of_day % 60U;
  const int32_t latitude_units = 126600 + chip->latitude_offset_1e4_min;
  const uint32_t latitude_minutes = (uint32_t)latitude_units / 10000U;
  const uint32_t latitude_fraction = (uint32_t)latitude_units % 10000U;
  const uint32_t effective_speed =
      (profile == PROFILE_MOVING) ? speed_kph : 0U;
  const uint32_t knots_hundredths =
      (effective_speed * 100000U + 926U) / 1852U;
  const bool valid_fix = profile != PROFILE_NO_FIX;
  const bool poor_hdop = profile == PROFILE_POOR_HDOP;

  char body[144];
  size_t used = 0U;
  int written = snprintf(
      body, sizeof(body),
      "GPGGA,%02u%02u%02u.00,04%02u.%04u,N,07404.3200,W,%u,%02u,%s,2600.0,M,0.0,M,,",
      hour, minute, second, latitude_minutes, latitude_fraction,
      valid_fix ? 1U : 0U, valid_fix ? 10U : 0U,
      valid_fix ? (poor_hdop ? "9.9" : "0.8") : "99.9");
  if (written < 0 || (size_t)written >= sizeof(body) ||
      !append_sentence(chip->tx_buffer, sizeof(chip->tx_buffer), &used, body)) {
    return false;
  }

  if (valid_fix) {
    written = snprintf(
        body, sizeof(body),
        "GPRMC,%02u%02u%02u.00,A,04%02u.%04u,N,07404.3200,W,%03u.%02u,000.0,%02u%02u%02u,,,A",
        hour, minute, second, latitude_minutes, latitude_fraction,
        knots_hundredths / 100U, knots_hundredths % 100U,
        chip->day, chip->month, chip->year);
  } else {
    written = snprintf(
        body, sizeof(body),
        "GPRMC,%02u%02u%02u.00,V,,,,,000.00,000.0,%02u%02u%02u,,,N",
        hour, minute, second, chip->day, chip->month, chip->year);
  }
  if (written < 0 || (size_t)written >= sizeof(body) ||
      !append_sentence(chip->tx_buffer, sizeof(chip->tx_buffer), &used, body)) {
    return false;
  }

  *frame_length = used;
  return true;
}

static void on_uart_write_done(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->uart_busy = false;
}

static void send_nmea(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->uart_busy) {
    return;
  }

  uint32_t profile = attr_read(chip->profile_attr);
  if (profile > PROFILE_NO_SIGNAL) {
    profile = PROFILE_MOVING;
  }
  uint32_t speed_kph = attr_read(chip->speed_kph_attr);
  if (speed_kph > 40U) {
    speed_kph = 40U;
  }
  if (profile != chip->last_profile) {
    printf("[nmea-gps] profile=%u speed_kph=%u\n", profile, speed_kph);
    chip->last_profile = profile;
  }

  if (profile == PROFILE_NO_SIGNAL) {
    advance_clock(chip);
    return;
  }

  size_t frame_length = 0U;
  if (build_frame(chip, profile, speed_kph, &frame_length) &&
      uart_write(chip->uart, (uint8_t *)chip->tx_buffer,
                 (uint32_t)frame_length)) {
    chip->uart_busy = true;
    if (profile == PROFILE_MOVING) {
      advance_position(chip, speed_kph);
    }
    advance_clock(chip);
  }
}

void chip_init() {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));
  if (chip == NULL) {
    return;
  }

  chip->profile_attr = attr_init("profile", PROFILE_MOVING);
  chip->speed_kph_attr = attr_init("speedKph", 12U);
  chip->last_profile = UINT32_MAX;
  chip->seconds_of_day = 20U * 3600U;
  chip->latitude_direction = 1;
  chip->day = 31U;
  chip->month = 7U;
  chip->year = 26U;

  const uart_config_t uart_config = {
      .tx = pin_init("TX", OUTPUT),
      .rx = NO_PIN,
      .baud_rate = 9600,
      .rx_data = NULL,
      .write_done = on_uart_write_done,
      .user_data = chip,
  };
  chip->uart = uart_init(&uart_config);

  const timer_config_t timer_config = {
      .callback = send_nmea,
      .user_data = chip,
  };
  chip->timer = timer_init(&timer_config);
  timer_start(chip->timer, 1000000, true);
}
