#include "display/identity.h"
#include <stdint.h>
#include <string.h>

namespace display {
PetName format_pet_name(const char *input) {
  PetName out;
  if (!input || !input[0]) return out;
  size_t bytes = 0;
  while (bytes < sizeof(out.text) && input[bytes]) ++bytes;
  const auto fail = [&](NameResult result) { PetName error; error.result = result; return error; };
  if (bytes >= sizeof(out.text)) return fail(NameResult::TooLong);
  unsigned count = 0;
  bool previous_space = true;
  for (size_t i = 0; i < bytes;) {
    uint32_t cp = static_cast<uint8_t>(input[i++]);
    unsigned continuation = 0;
    uint32_t minimum = 0;
    if (cp < 0x80) {}
    else if (cp >= 0xc2 && cp <= 0xdf) { cp &= 0x1f; continuation = 1; minimum = 0x80; }
    else if (cp >= 0xe0 && cp <= 0xef) { cp &= 0x0f; continuation = 2; minimum = 0x800; }
    else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; continuation = 3; minimum = 0x10000; }
    else return fail(NameResult::InvalidUtf8);
    if (i + continuation > bytes) return fail(NameResult::InvalidUtf8);
    while (continuation--) {
      const auto next = static_cast<uint8_t>(input[i++]);
      if ((next & 0xc0) != 0x80) return fail(NameResult::InvalidUtf8);
      cp = (cp << 6) | (next & 0x3f);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return fail(NameResult::InvalidUtf8);
    if (++count > 24) return fail(NameResult::TooLong);
    if (cp >= 0x300 && cp <= 0x36f) return fail(NameResult::NeedsNormalization);
    const bool allowed = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
      (cp >= '0' && cp <= '9') || cp == ' ' || cp == '-' || cp == '\'' ||
      (cp >= 0xc0 && cp <= 0xd6) || (cp >= 0xd8 && cp <= 0xf6) || (cp >= 0xf8 && cp <= 0xff);
    if (!allowed) return fail(NameResult::Unsupported);
    if (cp == ' ' && previous_space) return fail(NameResult::InvalidSpacing);
    previous_space = cp == ' ';
  }
  if (previous_space) return fail(NameResult::InvalidSpacing);
  memcpy(out.text, input, bytes);
  out.result = NameResult::Ready;
  return out;
}
}
