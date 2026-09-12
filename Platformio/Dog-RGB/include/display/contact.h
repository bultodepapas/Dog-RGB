#pragma once
#include <stddef.h>

namespace display {
// Stable IDs persisted by Identity record v1; do not renumber.
enum class QrContactKind { Disabled = 0, WhatsApp = 1, Call = 2 };
enum class ContactResult { Ready, Disabled, InvalidPhone, InvalidKind, NotInitialized, EncodeFailed };
struct ContactText {
  char phone[17]{}; // '+' and at most 15 digits, plus NUL.
  char payload[30]{};
  ContactResult result = ContactResult::InvalidPhone;
};
// Explicit international prefix; accepts spaces, hyphens and parentheses.
// Product syntax only: does not verify country allocation or account existence.
// Input must be NUL-terminated. Reads at most 64 bytes; rejects longer input.
ContactText format_contact(const char *phone, QrContactKind kind);
}
