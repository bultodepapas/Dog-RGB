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
  if (!prefs_instance.getBool("trk_part", false)) {
    Preferences legacy_track;
    if (legacy_track.begin("dogrgb_trk", false)) {
      legacy_track.clear();
      legacy_track.end();
    }
    prefs_instance.putBool("trk_part", true);
  }
  prefs_trk_instance.begin("dogrgb_trk", false, "tracknvs");
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
