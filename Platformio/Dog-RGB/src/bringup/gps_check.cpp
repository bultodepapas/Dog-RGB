#include "bringup/gps_check.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include "board/board_profile.h"
#include "bringup/bench_limits.h"
#include "config/runtime_config.h"
#include "gps/gps.h"
#include "led/led_ui.h"

#if !defined(DOG_RGB_BRINGUP_STAGE) || DOG_RGB_BRINGUP_STAGE != 2
#error "GPS check belongs only to bringup stage 2"
#endif

namespace bringup::gps_check {
static_assert(!DEBUG_AP_ONLY_MINIMAL && LED_UI_ENABLED,
              "I2 requires the normal GPS/LED application path");
void report(Print &sink) {
  const uint32_t now = millis();
  const gps::ReceptionState state = gps::reception_state();
  char speed[20] = "--";
  if (state == gps::ReceptionState::Fix && gps::speed_usable() &&
      isfinite(gps::last_speed_kph())) {
    snprintf(speed, sizeof(speed), "%.2f", static_cast<double>(gps::last_speed_kph()));
  }
  const RuntimeConfig &cfg = config::get();
  const led::PowerLimitConfig power = bench_limits::power({cfg.led_power_limit_enabled,
      cfg.led_power_budget_ma, cfg.led_base_current_ma,
      cfg.led_rgb_channel_ma, cfg.led_white_channel_ma});
  const uint8_t requested = LED_DEBUG_BRIGHTNESS_ENABLED ? LED_DEBUG_BRIGHTNESS : cfg.brightness;
  char line[512];
  const int length = snprintf(line, sizeof(line),
      "[I2] ms=%lu state=%s raw=%d trusted=%d speed_kph=%s day_m=%.1f date=%lu "
      "sats=%u quality=%u hdop=%.2f uart_seen=%d uart_age_ms=%lu "
      "rmc_seen=%d rmc_age_ms=%lu rx=%lu rmc=%lu gga=%lu stale=%lu "
      "overflow=%lu checksum=%lu parse=%lu mode=%u brightness=%u/%u "
      "budget_ma=%u estimated_ma=%u\n",
      static_cast<unsigned long>(now), gps::reception_name(state), gps::raw_fix(),
      gps::trusted_fix(), speed, static_cast<double>(gps::total_distance_m()),
      static_cast<unsigned long>(gps::current_date()), gps::sats(), gps::fix_quality(),
      static_cast<double>(gps::hdop()), gps::has_byte_observation(),
      static_cast<unsigned long>(gps::has_byte_observation() ?
          time_utils::age_ms(now, gps::last_byte_ms()) : 0),
      gps::has_rmc_observation(), static_cast<unsigned long>(gps::has_rmc_observation() ?
          time_utils::age_ms(now, gps::last_rmc_ms()) : 0),
      gps::bytes_rx(), gps::rmc_seen(), gps::gga_seen(), gps::stale_count(),
      gps::overflow(), gps::checksum_fail(), gps::parse_fail(), cfg.mode,
      bench_limits::brightness(requested), requested, power.budget_ma,
      led_ui::power_diagnostics().estimated_ma);
  if (length > 0 && static_cast<size_t>(length) < sizeof(line)) {
    sink.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(length));
  } else {
    sink.print("[I2] report-too-long\n");
  }
}
} // namespace bringup::gps_check
