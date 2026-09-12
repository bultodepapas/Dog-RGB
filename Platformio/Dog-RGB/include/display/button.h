#pragma once
#include <stdint.h>

namespace display {
// Active-low BOOT input: debounced release, no hold/repeat/power action.
class ReleaseButton {
 public:
  void begin(bool down, uint32_t now) {
    raw_ = stable_ = down; changed_ = pressed_ = now; armed_ = false;
  }
  bool update(bool down, uint32_t now) {
    if (raw_ != down) { raw_ = down; changed_ = now; }
    if (raw_ == stable_ || uint32_t(now - changed_) < 30) return false;
    stable_ = raw_;
    if (stable_) { pressed_ = now; armed_ = true; return false; }
    const bool click = armed_ && uint32_t(now - pressed_) < 1500;
    armed_ = false;
    return click;
  }
 private:
  bool raw_ = false, stable_ = false, armed_ = false;
  uint32_t changed_ = 0, pressed_ = 0;
};
} // namespace display
