#include "storage/nvs_store.h"

namespace storage {
namespace {
Preferences prefs_instance;
Preferences prefs_cfg_instance;
} // namespace

void begin() {
  prefs_instance.begin("dogrgb", false);
  prefs_cfg_instance.begin("dogrgb_cfg", false);
}

Preferences &prefs() {
  return prefs_instance;
}

Preferences &prefs_cfg() {
  return prefs_cfg_instance;
}
} // namespace storage
