#include "web/portal_lock.h"

#include <stddef.h>
#include <string.h>

#include "storage/nvs_store.h"
#include "util/crc32.h"

namespace portal_lock {
namespace {

static const char *LOCK_KEY = "portal_lock";
static const uint32_t LOCK_MAGIC = 0x4B434C44UL; // "DLCK" in storage.
static const uint16_t LOCK_VERSION = 1;

struct __attribute__((packed)) LockRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  char pin[9];
  uint8_t reserved;
  uint32_t crc32;
};

LockRecord state = {};
bool loaded = false;

uint32_t record_crc(const LockRecord &record) {
  return util::crc32_ieee(&record, offsetof(LockRecord, crc32));
}

void clear_state() {
  memset(&state, 0, sizeof(state));
  state.magic = LOCK_MAGIC;
  state.version = LOCK_VERSION;
  state.size = sizeof(state);
}

// A corrupt or truncated record leaves the portal unlocked rather than
// unreachable. This is an optional convenience on a hobby device; bricking a
// user's configuration UI is the worse failure of the two.
bool record_valid(const LockRecord &record) {
  return record.magic == LOCK_MAGIC &&
         record.version == LOCK_VERSION &&
         record.size == sizeof(record) &&
         memchr(record.pin, '\0', sizeof(record.pin)) != nullptr &&
         record.crc32 == record_crc(record);
}

} // namespace

void begin() {
  clear_state();
  loaded = true;

  Preferences &prefs = storage::prefs_cfg();
  LockRecord stored = {};
  if (prefs.getBytesLength(LOCK_KEY) != sizeof(stored)) {
    return;
  }
  if (prefs.getBytes(LOCK_KEY, &stored, sizeof(stored)) != sizeof(stored)) {
    return;
  }
  if (!record_valid(stored)) {
    return;
  }
  state = stored;
}

bool enabled() {
  return loaded && state.pin[0] != '\0';
}

bool valid_pin(const String &pin) {
  if (pin.length() < 4 || pin.length() > 8) {
    return false;
  }
  for (size_t i = 0; i < pin.length(); ++i) {
    if (pin[i] < '0' || pin[i] > '9') {
      return false;
    }
  }
  return true;
}

bool accepts(const String &pin) {
  if (!enabled()) {
    return true;
  }
  if (pin.length() != strlen(state.pin)) {
    return false;
  }
  // Compare every byte so a wrong PIN takes the same time as a right one.
  uint8_t diff = 0;
  for (size_t i = 0; i < pin.length(); ++i) {
    diff |= static_cast<uint8_t>(pin[i] ^ state.pin[i]);
  }
  return diff == 0;
}

bool set_pin(const String &pin) {
  LockRecord next = {};
  next.magic = LOCK_MAGIC;
  next.version = LOCK_VERSION;
  next.size = sizeof(next);

  if (pin.length() > 0) {
    if (!valid_pin(pin)) {
      return false;
    }
    memcpy(next.pin, pin.c_str(), pin.length());
    next.pin[pin.length()] = '\0';
  }
  next.crc32 = record_crc(next);

  Preferences &prefs = storage::prefs_cfg();
  if (prefs.putBytes(LOCK_KEY, &next, sizeof(next)) != sizeof(next)) {
    return false;
  }
  state = next;
  loaded = true;
  return true;
}

} // namespace portal_lock
