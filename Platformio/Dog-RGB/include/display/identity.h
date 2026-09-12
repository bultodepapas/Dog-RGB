#pragma once
#include "display/contact.h"

namespace display {
enum class NameResult { Ready, Empty, TooLong, InvalidUtf8, Unsupported, NeedsNormalization, InvalidSpacing };
struct PetName {
  char text[49]{};
  NameResult result = NameResult::Empty;
};
// NFC Latin-1 letters, ASCII letters/digits, space, hyphen and apostrophe.
// At most 48 UTF-8 bytes/24 code points; input must be NUL-terminated.
// Does not silently strip, transliterate or normalize user input.
PetName format_pet_name(const char *input);
}
