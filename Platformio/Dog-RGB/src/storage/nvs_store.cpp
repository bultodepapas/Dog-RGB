#include "storage/nvs_store.h"

namespace storage {
namespace {
Preferences prefs_instance;
Preferences prefs_cfg_instance;
Preferences prefs_trk_instance;
} // namespace

void begin() {
  prefs_instance.begin("dogrgb", false);
  prefs_cfg_instance.begin("dogrgb_cfg", false);
  prefs_trk_instance.begin("dogrgb_trk", false);
}

Preferences &prefs() {
  return prefs_instance;
}

Preferences &prefs_cfg() {
  return prefs_cfg_instance;
}

Preferences &prefs_trk() {
  return prefs_trk_instance;
}
} // namespace storage
