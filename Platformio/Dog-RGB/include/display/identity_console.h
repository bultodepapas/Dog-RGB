#pragma once
#include <stddef.h>
#include <stdint.h>

namespace display {
// Stage-3 bench transport. After rejection, drain through LF so payload bytes
// can never become unrelated single-character LCD commands.
class IdentityConsole {
 public:
  void begin(uint32_t now);
  bool active() const { return active_; }
  const char *poll(uint32_t now);
  const char *feed(uint8_t byte, uint32_t now);
 private:
  const char *apply();
  char body_[513]{};
  size_t size_ = 0;
  uint32_t started_ = 0;
  bool active_ = false;
  const char *rejected_ = nullptr;
};
}
