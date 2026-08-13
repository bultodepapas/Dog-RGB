#include "storage/scene_nvs_backend.h"

#include <Preferences.h>

#include "storage/nvs_store.h"

namespace storage {
namespace {

static const char *const BANK_KEYS[2] = {"scene_a", "scene_b"};
// ESP-IDF NVS blobs use two metadata entries plus ceil(payload / 32).
static const size_t RECORD_NVS_ENTRIES = 9;

static const char *bank_key(uint8_t bank_index) {
  return bank_index < 2U ? BANK_KEYS[bank_index] : nullptr;
}

} // namespace

SceneBackendRead SceneNvsBackend::read(uint8_t bank_index, uint8_t *out,
                                       size_t capacity,
                                       size_t &actual_size) {
  actual_size = 0;
  const char *key = bank_key(bank_index);
  if (!scenes_available() || key == nullptr || out == nullptr) {
    return SceneBackendRead::Error;
  }
  Preferences &preferences = prefs_scenes();
  const size_t stored_size = preferences.getBytesLength(key);
  if (stored_size == 0U) return SceneBackendRead::Missing;
  actual_size = stored_size;
  if (stored_size > capacity) return SceneBackendRead::TooLarge;
  const size_t read_size = preferences.getBytes(key, out, stored_size);
  return read_size == stored_size ? SceneBackendRead::Ok
                                  : SceneBackendRead::Error;
}

SceneBackendWrite SceneNvsBackend::write(uint8_t bank_index,
                                         const uint8_t *data, size_t size,
                                         size_t &written_size) {
  written_size = 0;
  const char *key = bank_key(bank_index);
  if (!scenes_available() || key == nullptr || data == nullptr || size == 0U) {
    return SceneBackendWrite::Error;
  }
  Preferences &preferences = prefs_scenes();
  written_size = preferences.putBytes(key, data, size);
  if (written_size == size) return SceneBackendWrite::Ok;
  return preferences.freeEntries() < RECORD_NVS_ENTRIES
             ? SceneBackendWrite::Full
             : SceneBackendWrite::Error;
}

size_t SceneNvsBackend::free_entries() const {
  return scenes_available() ? prefs_scenes().freeEntries() : 0U;
}

} // namespace storage
