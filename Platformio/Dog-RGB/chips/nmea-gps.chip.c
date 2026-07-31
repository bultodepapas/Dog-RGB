#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wokwi-api.h"

typedef struct {
  uart_dev_t uart;
  timer_t timer;
  uint32_t frame_index;
} chip_state_t;

static const char *const NMEA_FRAMES[] = {
    "$GPRMC,120000.00,A,0412.6600,N,07404.3200,W,005.0,090.0,310726,,,A*43\r\n"
    "$GPGGA,120000.00,0412.6600,N,07404.3200,W,1,10,0.8,2600.0,M,0.0,M,,*71\r\n",
    "$GPRMC,120005.00,A,0412.6660,N,07404.3140,W,005.0,090.0,310726,,,A*47\r\n"
    "$GPGGA,120005.00,0412.6660,N,07404.3140,W,1,10,0.8,2600.0,M,0.0,M,,*75\r\n",
    "$GPRMC,120010.00,A,0412.6720,N,07404.3080,W,005.0,090.0,310726,,,A*4B\r\n"
    "$GPGGA,120010.00,0412.6720,N,07404.3080,W,1,10,0.8,2600.0,M,0.0,M,,*79\r\n",
};

static void send_nmea(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const char *frame = NMEA_FRAMES[chip->frame_index];
  if (uart_write(chip->uart, (uint8_t *)frame, (uint32_t)strlen(frame))) {
    chip->frame_index = (chip->frame_index + 1U) %
                        (sizeof(NMEA_FRAMES) / sizeof(NMEA_FRAMES[0]));
  }
}

void chip_init() {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));

  const uart_config_t uart_config = {
      .tx = pin_init("TX", OUTPUT),
      .rx = NO_PIN,
      .baud_rate = 9600,
      .rx_data = NULL,
      .write_done = NULL,
      .user_data = chip,
  };
  chip->uart = uart_init(&uart_config);

  const timer_config_t timer_config = {
      .callback = send_nmea,
      .user_data = chip,
  };
  chip->timer = timer_init(&timer_config);
  timer_start(chip->timer, 100000, true);
}
