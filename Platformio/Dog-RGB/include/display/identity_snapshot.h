#pragma once
#include "display/identity.h"

namespace display {
// Owned copy from the main-loop store. Rendering never reads or writes NVS.
struct IdentitySnapshot {
  char name[49]{};
  char phone[17]{};
  QrContactKind kind = QrContactKind::Disabled;
};
IdentitySnapshot capture_identity();
}
