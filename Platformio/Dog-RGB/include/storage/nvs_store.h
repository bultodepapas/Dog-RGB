#pragma once

#include <Preferences.h>

namespace storage {
void begin();
Preferences &prefs();
Preferences &prefs_cfg();
Preferences &prefs_trk();
}
