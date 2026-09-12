#pragma once
#include <stdint.h>

namespace display {
// UI interaction clock only. No domain activity, allocations or persistence.
class InactivityTimer {
 public:
  void configure(uint32_t timeout_ms, uint32_t now) {
    timeout_ms_ = timeout_ms;
    activity(now);
  }
  void activity(uint32_t now) { last_activity_ = now; expired_ = false; }
  bool poll(uint32_t now) {
    if (timeout_ms_ && !expired_ && uint32_t(now - last_activity_) >= timeout_ms_) {
      expired_ = true;
      ++expirations_;
    }
    return expired_; // Latch across clock wrap until an explicit interaction.
  }
  bool expired() const { return expired_; }
  uint32_t timeout_ms() const { return timeout_ms_; }
  uint32_t expirations() const { return expirations_; }
 private:
  uint32_t timeout_ms_ = 0, last_activity_ = 0, expirations_ = 0;
  bool expired_ = false;
};
} // namespace display
