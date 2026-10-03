#include "track/device_identity_nvs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << "line " << __LINE__ << ": " << #expr << '\n'; \
  std::exit(1); } } while (false)

namespace {
using track::identity::NvsBackend;
using track::identity::ReadStatus;
using track::identity::kRecordSize;
std::map<std::string, std::vector<uint8_t> > durable, pending;
bool opened = false;
unsigned opens = 0, closes = 0, queries = 0, fetches = 0, sets = 0, commits = 0;
esp_err_t open_error = ESP_OK, query_error = ESP_OK, fetch_error = ESP_OK;
esp_err_t set_error = ESP_OK, commit_error = ESP_OK;
bool short_fetch = false, persist_on_failed_commit = false;
bool persist_on_failed_set = false;
static_assert(!std::is_copy_constructible<NvsBackend>::value, "one NVS handle owner");
static_assert(!std::is_move_constructible<NvsBackend>::value, "stable NVS handle owner");

void check_handle(nvs_handle_t handle) { CHECK(opened && handle == 7); }
void check_key(const char *key) {
  CHECK(!std::strcmp(key, "id_a") || !std::strcmp(key, "id_b"));
}
} // namespace

esp_err_t nvs_open_from_partition(const char *partition, const char *name,
                                  nvs_open_mode_t mode, nvs_handle_t *handle) {
  CHECK(!std::strcmp(partition, "nvs"));
  CHECK(!std::strcmp(name, "dogrgb_v3"));
  CHECK(mode == NVS_READWRITE);
  ++opens;
  if (open_error != ESP_OK) return open_error;
  CHECK(!opened);
  opened = true;
  *handle = 7;
  return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
  check_handle(handle);
  opened = false;
  ++closes;
  pending.clear();
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
  check_handle(handle);
  check_key(key);
  if (out == nullptr) {
    ++queries;
    if (query_error != ESP_OK) return query_error;
  } else {
    ++fetches;
    if (fetch_error != ESP_OK) return fetch_error;
  }
  const auto found = durable.find(key);
  if (found == durable.end()) return ESP_ERR_NVS_NOT_FOUND;
  const auto &bytes = found->second;
  if (out != nullptr) {
    if (*size < bytes.size()) return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, bytes.data(), bytes.size());
  }
  *size = bytes.size() - ((out != nullptr && short_fetch) ? 1U : 0U);
  return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
  check_handle(handle);
  check_key(key);
  ++sets;
  const auto *bytes = static_cast<const uint8_t *>(data);
  if (set_error != ESP_OK) {
    if (persist_on_failed_set) durable[key] = std::vector<uint8_t>(bytes, bytes + size);
    return set_error;
  }
  pending[key] = std::vector<uint8_t>(bytes, bytes + size);
  return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
  check_handle(handle);
  ++commits;
  if (commit_error == ESP_OK || persist_on_failed_commit) {
    for (const auto &entry : pending) durable[entry.first] = entry.second;
    pending.clear();
  }
  return commit_error;
}

int main() {
  uint8_t data[kRecordSize];
  std::fill(data, data + kRecordSize, 0xA5);
  uint8_t out[kRecordSize] = {};
  size_t actual = 99;
  {
    NvsBackend backend;
    CHECK(opens == 0 && sets == 0 && !backend.is_open());
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kError && actual == 0);
    CHECK(!backend.write(0, data, sizeof(data)) && sets == 0);
    open_error = ESP_FAIL;
    CHECK(!backend.open() && !backend.is_open() && closes == 0);
    open_error = ESP_OK;
    CHECK(backend.open() && backend.open() && opens == 2);
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kMissing);
    CHECK(actual == 0 && queries == 1 && fetches == 0);
    CHECK(backend.read(2, out, sizeof(out), actual) == ReadStatus::kError);
    CHECK(backend.read(0, nullptr, sizeof(out), actual) == ReadStatus::kError);
    CHECK(!backend.write(2, data, sizeof(data)));
    CHECK(!backend.write(0, nullptr, sizeof(data)));
    CHECK(!backend.write(0, data, sizeof(data) - 1) && sets == 0);

    query_error = ESP_ERR_NVS_TYPE_MISMATCH;
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kError);
    query_error = ESP_FAIL;
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kError);
    query_error = ESP_OK;
    durable["id_a"] = {};
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kOk && actual == 0);
    CHECK(fetches == 0);
    durable["id_a"].resize(kRecordSize + 1);
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kTooLarge);
    CHECK(actual == kRecordSize + 1 && fetches == 0);

    CHECK(backend.write(0, data, sizeof(data)) && commits == 1);
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kOk);
    CHECK(actual == sizeof(data) && !std::memcmp(data, out, sizeof(data)));
    fetch_error = ESP_ERR_NVS_NOT_FOUND; // disappeared between length and fetch
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kError);
    fetch_error = ESP_ERR_NVS_INVALID_LENGTH;
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kError);
    fetch_error = ESP_OK;
    short_fetch = true;
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kError);
    short_fetch = false;

    set_error = ESP_FAIL;
    CHECK(!backend.write(1, data, sizeof(data)) && !backend.is_open());
    CHECK(commits == 1 && closes == 1);
    CHECK(!backend.write(1, data, sizeof(data)) && commits == 1);
    set_error = ESP_OK;
    CHECK(backend.open());
    commit_error = ESP_FAIL;
    CHECK(!backend.write(1, data, sizeof(data)) && !backend.is_open());
    CHECK(commits == 2 && closes == 2 && !durable.count("id_b"));
    CHECK(backend.open());
    persist_on_failed_commit = true;
    CHECK(!backend.write(1, data, sizeof(data)) && !backend.is_open());
    CHECK(commits == 3 && closes == 3 && durable.count("id_b") == 1);
    CHECK(backend.open());
    CHECK(backend.read(1, out, sizeof(out), actual) == ReadStatus::kOk);
    CHECK(!std::memcmp(data, out, sizeof(data)));
    backend.close();
    backend.close();
    CHECK(closes == 4);
  }
  CHECK(closes == 4 && !opened); // destructor cannot close an already closed handle
  {
    NvsBackend backend;
    CHECK(backend.open());
  }
  CHECK(closes == 5 && !opened);
  {
    NvsBackend backend;
    CHECK(backend.open());
    data[0] = 0x7F;
    set_error = ESP_FAIL;
    persist_on_failed_set = true;
    CHECK(!backend.write(0, data, sizeof(data)) && !backend.is_open());
    CHECK(closes == 6 && commits == 3);
    set_error = ESP_OK;
    CHECK(backend.open());
    CHECK(backend.read(0, out, sizeof(out), actual) == ReadStatus::kOk);
    CHECK(!std::memcmp(data, out, sizeof(data)));
  }
  CHECK(closes == 7 && !opened);
  std::cout << "device_identity_nvs: ok\n";
}
