#if DOG_RGB_BRINGUP_STAGE == 3 && DOG_RGB_DISPLAY_LVGL == 1
#include "display/identity_console.h"
#include "display/identity_store.h"
#include <ArduinoJson.h>
#include <string.h>

namespace display {
void IdentityConsole::begin(uint32_t now) {
  memset(body_, 0, sizeof(body_)); size_ = 0; started_ = now;
  rejected_ = nullptr; active_ = true;
}
const char *IdentityConsole::poll(uint32_t now) {
  if (active_ && !rejected_ && now - started_ >= 5000) {
    rejected_ = "timeout";
    memset(body_, 0, sizeof(body_)); size_ = 0;
    return rejected_;
  }
  return nullptr;
}
const char *IdentityConsole::feed(uint8_t byte, uint32_t now) {
  if (!active_) return nullptr;
  const char *expired = poll(now);
  if (byte == '\n') {
    active_ = false;
    const char *result = rejected_ ? (expired ? expired : nullptr) : size_ ? apply() : "cancelled";
    memset(body_, 0, sizeof(body_)); size_ = 0;
    rejected_ = nullptr;
    return result;
  }
  if (rejected_) return expired;
  if (byte == 0 || size_ == sizeof(body_) - 1) {
    rejected_ = byte == 0 ? "fields" : "body_size";
    memset(body_, 0, sizeof(body_)); size_ = 0;
    return rejected_;
  }
  body_[size_++] = static_cast<char>(byte);
  return nullptr;
}
const char *IdentityConsole::apply() {
  JsonDocument doc;
  if (deserializeJson(doc, body_, size_, DeserializationOption::NestingLimit(2)) ||
      !doc.is<JsonObject>() || doc.size() != 4 ||
      !doc["name"].is<const char *>() || !doc["phone"].is<const char *>() ||
      !doc["qr_kind"].is<const char *>() || !doc["expected_generation"].is<uint32_t>()) return "fields";
  const JsonString name = doc["name"].as<JsonString>(), phone = doc["phone"].as<JsonString>(), kind = doc["qr_kind"].as<JsonString>();
  if (name.size() != strlen(name.c_str()) || phone.size() != strlen(phone.c_str()) || kind.size() != strlen(kind.c_str())) return "fields";
  QrContactKind channel;
  if (!strcmp(kind.c_str(), "whatsapp")) channel = QrContactKind::WhatsApp;
  else if (!strcmp(kind.c_str(), "call")) channel = QrContactKind::Call;
  else if (!strcmp(kind.c_str(), "disabled")) channel = QrContactKind::Disabled;
  else return "qr_kind";
  const auto result = identity::save(name.c_str(), phone.c_str(), channel, doc["expected_generation"].as<uint32_t>());
  using Result = identity::SaveResult;
  switch (result) {
    case Result::Saved: return "saved";
    case Result::Unchanged: return "unchanged";
    case Result::Conflict: return "conflict";
    case Result::InvalidName: return "name";
    case Result::InvalidPhone: return "phone";
    case Result::InvalidKind: return "qr_kind";
    default: return "storage";
  }
}
}
#endif
