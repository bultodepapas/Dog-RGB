#pragma once
#include "display/identity.h"
#include <stdint.h>

namespace identity {
struct Config {
  char name[49]{};
  char phone[17]{};
  display::QrContactKind kind = display::QrContactKind::Disabled;
};
enum class SaveResult { Saved, Unchanged, InvalidName, InvalidPhone, InvalidKind, Conflict, Storage, NotLoaded };
void load(); // After storage::begin(), on the main/portal task.
const Config &get();
uint32_t generation();
bool configured();
SaveResult save(const char *name, const char *phone, display::QrContactKind kind, uint32_t expected_generation);
}
