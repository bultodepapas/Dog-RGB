#pragma once

namespace board {
// Call before Serial/storage/radio. No strip transport, sensor or LCD driver.
void begin();
void write_status(bool on);
} // namespace board
