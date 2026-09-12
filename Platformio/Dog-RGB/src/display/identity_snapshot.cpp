#if DOG_RGB_DISPLAY_LVGL == 1
#include "display/identity_snapshot.h"
#include "display/identity_store.h"
#include <string.h>

namespace display {
IdentitySnapshot capture_identity() {
  const auto &config = identity::get();
  IdentitySnapshot result;
  memcpy(result.name, config.name, sizeof(result.name));
  memcpy(result.phone, config.phone, sizeof(result.phone));
  result.kind = config.kind;
  return result;
}
}
#endif
