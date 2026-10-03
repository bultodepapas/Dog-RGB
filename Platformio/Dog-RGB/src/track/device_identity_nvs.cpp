#include "track/device_identity_nvs.h"

namespace track {
namespace identity {
namespace {
const char *bank_key(uint8_t slot) {
  static const char *const keys[] = {"id_a", "id_b"};
  return slot < 2 ? keys[slot] : nullptr;
}
} // namespace

NvsBackend::NvsBackend() : handle_(0), opened_(false) {}
NvsBackend::~NvsBackend() { close(); }

bool NvsBackend::open() {
  if (opened_) return true;
  nvs_handle_t next = 0;
  if (nvs_open_from_partition("nvs", "dogrgb_v3", NVS_READWRITE, &next) !=
      ESP_OK) {
    return false;
  }
  handle_ = next;
  opened_ = true;
  return true;
}

void NvsBackend::close() {
  if (opened_) nvs_close(handle_);
  opened_ = false;
  handle_ = 0;
}

bool NvsBackend::is_open() const { return opened_; }

ReadStatus NvsBackend::read(uint8_t slot, uint8_t *out, size_t capacity,
                            size_t &actual_size) {
  actual_size = 0;
  const char *key = bank_key(slot);
  if (!opened_ || key == nullptr || out == nullptr) return ReadStatus::kError;
  size_t stored_size = 0;
  const esp_err_t query = nvs_get_blob(handle_, key, nullptr, &stored_size);
  if (query == ESP_ERR_NVS_NOT_FOUND) return ReadStatus::kMissing;
  if (query != ESP_OK) return ReadStatus::kError;
  actual_size = stored_size;
  if (stored_size > capacity) return ReadStatus::kTooLarge;
  // A present zero-length blob is not an empty bank; let the record decoder
  // reject its length without accidentally classifying it as Missing.
  if (stored_size == 0) return ReadStatus::kOk;
  size_t fetched_size = stored_size;
  const esp_err_t fetched = nvs_get_blob(handle_, key, out, &fetched_size);
  if (fetched != ESP_OK || fetched_size != stored_size) return ReadStatus::kError;
  return ReadStatus::kOk;
}

bool NvsBackend::write(uint8_t slot, const uint8_t *data, size_t size) {
  const char *key = bank_key(slot);
  if (!opened_ || key == nullptr || data == nullptr || size != kRecordSize) {
    return false;
  }
  if (nvs_set_blob(handle_, key, data, size) != ESP_OK ||
      nvs_commit(handle_) != ESP_OK) {
    // Stop further writes through this handle. Closing does not imply rollback;
    // recovery requires reopen and a fresh identity Store.
    close();
    return false;
  }
  return true; // Store still requires an exact readback before publishing.
}

} // namespace identity
} // namespace track
