#include "board/board_io.h"

#include <Arduino.h>
#include "board/board_profile.h"

namespace board {
void begin() {
  if constexpr (kPowerHoldPin >= 0) {
    pinMode(kPowerHoldPin, OUTPUT);
    digitalWrite(kPowerHoldPin, HIGH);
    pinMode(kPowerButtonPin, INPUT);
  }
  if constexpr (kBacklightPin >= 0) {
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, LOW);
  }
  if constexpr (kStatusLedPin >= 0) {
    pinMode(kStatusLedPin, OUTPUT);
    write_status(false);
  }
}

void write_status(bool on) {
  if constexpr (kStatusLedPin >= 0) {
    digitalWrite(kStatusLedPin, on ? HIGH : LOW);
  }
}
} // namespace board
