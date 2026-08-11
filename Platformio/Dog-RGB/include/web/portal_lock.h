#pragma once

#include <Arduino.h>

// Optional PIN gate for the portal's write endpoints.
//
// Off by default: a freshly built collar behaves exactly as it did before this
// existed, with no setup step. Reads are never gated, so anyone with the Wi-Fi
// password can still open the dashboard on their phone.
//
// Deliberately stored outside RuntimeConfig. The config record is a
// size- and CRC-checked A/B blob; growing it would invalidate every record
// already written and reset a user's tuned configuration on the next update.
namespace portal_lock {

// Reads the stored PIN. Safe to call before any other function.
void begin();

bool enabled();

// True when the supplied PIN matches the stored one, or when the lock is off.
bool accepts(const String &pin);

// A valid PIN is 4-8 digits: long enough to be a deliberate secret, short
// enough to type on a phone keypad.
bool valid_pin(const String &pin);

// Enables the lock with `pin`, or disables it when `pin` is empty. Returns
// false if the PIN is malformed or the write failed.
bool set_pin(const String &pin);

} // namespace portal_lock
