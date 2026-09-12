#include "display/contact.h"
#include <string.h>

namespace display {
ContactText format_contact(const char *input, QrContactKind kind) {
  ContactText out;
  if (!input) return out;
  size_t n = 0, digits = 0;
  bool plus = false;
  for (; n < 64 && input[n]; ++n) {
    const char c = input[n];
    if (c == ' ' || c == '-' || c == '(' || c == ')') continue;
    if (c == '+' && !plus && digits == 0) { plus = true; continue; }
    if (!plus || c < '0' || c > '9' || digits == 15) return ContactText{};
    out.phone[1 + digits++] = c;
  }
  if (n == 64 || !plus || digits < 7 || out.phone[1] == '0') return ContactText{};
  out.phone[0] = '+';
  if (kind == QrContactKind::Disabled) out.result = ContactResult::Disabled;
  else if (kind == QrContactKind::WhatsApp) {
    memcpy(out.payload, "https://wa.me/", 14);
    memcpy(out.payload + 14, out.phone + 1, digits);
    out.result = ContactResult::Ready;
  } else if (kind == QrContactKind::Call) {
    memcpy(out.payload, "tel:", 4);
    memcpy(out.payload + 4, out.phone, digits + 1);
    out.result = ContactResult::Ready;
  } else out.result = ContactResult::InvalidKind;
  return out;
}
}
