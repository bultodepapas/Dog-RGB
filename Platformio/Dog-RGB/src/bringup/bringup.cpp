#include "bringup/bringup.h"

#include <Arduino.h>
#include <esp_system.h>
#include "board/board_profile.h"
#if DOG_RGB_BRINGUP_STAGE == 1
#include "bringup/led_check.h"
#endif

#if !defined(DOG_RGB_BRINGUP_STAGE)
#error "Bringup must not be compiled into a product target"
#endif

namespace bringup {
namespace {
uint32_t last_report_ms = 0;
void report(uint32_t now_ms) {
  // One bounded line per second. No wait for USB and no per-loop logging.
  if (!Serial) {
    return;
  }
  Serial.printf("[I%d] board=%s revision=%s stage=%d uptime_ms=%lu reset=%d "
                "flash=%lu/%lu psram=%lu/%lu heap=%lu min_heap=%lu "
                "sys_out=%d lcd=off external_io=%s\n",
                DOG_RGB_BRINGUP_STAGE, board::kId, board::kRevision, DOG_RGB_BRINGUP_STAGE,
                static_cast<unsigned long>(now_ms), static_cast<int>(esp_reset_reason()),
                static_cast<unsigned long>(ESP.getFlashChipSize()), board::kFlashBytes,
                static_cast<unsigned long>(ESP.getPsramSize()), board::kPsramBytes,
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()),
                digitalRead(board::kPowerButtonPin),
                DOG_RGB_BRINGUP_STAGE == 0 ? "inactive" : "led-check");
#if DOG_RGB_BRINGUP_STAGE == 1
  led_check::report();
#endif
}
} // namespace

void begin() {
#if DOG_RGB_BRINGUP_STAGE == 1
  led_check::begin();
#endif
  last_report_ms = millis();
  report(last_report_ms);
}

void tick(uint32_t now_ms) {
#if DOG_RGB_BRINGUP_STAGE == 1
  led_check::tick(now_ms);
#endif
  if (static_cast<uint32_t>(now_ms - last_report_ms) >= 1000U) {
    last_report_ms = now_ms;
    report(now_ms);
  }
}
} // namespace bringup
