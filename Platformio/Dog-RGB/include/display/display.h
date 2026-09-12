#pragma once
#include <stdint.h>
class Print;

namespace display {
// One initialization attempt; a detected failure leaves only the UI disabled.
bool begin();
void tick();
void report(Print &sink);
} // namespace display
