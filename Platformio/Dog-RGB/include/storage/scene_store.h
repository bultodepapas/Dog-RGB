#pragma once

#include <stddef.h>
#include <stdint.h>

#include "led/scene_catalog.h"

namespace storage {

static const uint16_t SCENE_RECORD_VERSION = 1;
static const size_t SCENE_RECORD_BYTES = 196;
static const size_t SCENE_RECORD_READ_MAX = 512;

enum class SceneBackendRead : uint8_t {
  Ok = 0,
  Missing,
  TooLarge,
  Error,
};

enum class SceneBackendWrite : uint8_t {
  Ok = 0,
  Full,
  Error,
};

enum class SceneRecordStatus : uint8_t {
  Missing = 0,
  Valid,
  Corrupt,
  Future,
  Oversized,
  IoError,
};

enum class SceneStoreHealth : uint8_t {
  Uninitialized = 0,
  Empty,
  Healthy,
  Recovered,
  DegradedEmpty,
  Corrupt,
  Ambiguous,
  ReadOnlyFuture,
  OversizedUnknown,
  Unavailable,
  Uncertain,
};

enum class SceneStoreResultCode : uint8_t {
  Ok = 0,
  NoChange,
  InvalidSlot,
  InvalidScene,
  GenerationConflict,
  RecoveryRequired,
  ReadOnly,
  Unavailable,
  StorageFull,
  WriteFailed,
  VerifyFailed,
  Uncertain,
};

struct SceneStoreResult {
  SceneStoreResultCode code;
  bool no_change;
  uint32_t generation;
  SceneStoreHealth health;
  led::SceneValidationError validation_error;
};

struct SceneStoreDiagnostics {
  SceneStoreHealth health;
  SceneRecordStatus bank_a;
  SceneRecordStatus bank_b;
  int8_t active_bank;
  uint32_t generation;
  uint32_t bank_a_generation;
  uint32_t bank_b_generation;
  uint32_t load_count;
  uint32_t recovery_count;
  uint32_t mutation_count;
  uint32_t write_failures;
  uint32_t verify_failures;
  uint32_t uncertain_commits;
  size_t free_entries;
  uint32_t last_write_us;
  uint32_t max_write_us;
};

class SceneRecordBackend {
 public:
  virtual ~SceneRecordBackend() {}
  virtual SceneBackendRead read(uint8_t bank_index, uint8_t *out,
                                size_t capacity, size_t &actual_size) = 0;
  virtual SceneBackendWrite write(uint8_t bank_index, const uint8_t *data,
                                  size_t size, size_t &written_size) = 0;
  virtual size_t free_entries() const = 0;
};

struct DecodedSceneRecord {
  led::SceneBank bank;
  uint32_t generation;
  uint16_t record_version;
};

bool encode_scene_record(const led::SceneBank &bank, uint32_t generation,
                         uint8_t *out, size_t out_size);
SceneRecordStatus decode_scene_record(const uint8_t *data, size_t data_size,
                                      DecodedSceneRecord &out);
bool scene_generation_is_newer(uint32_t candidate, uint32_t reference);
bool scene_generation_is_ambiguous(uint32_t left, uint32_t right);

class SceneStore {
 public:
  SceneStore(SceneRecordBackend &backend, led::SceneCatalog &catalog);

  SceneStoreHealth load();
  SceneStoreResult save_slot(uint8_t slot_one_based,
                             const led::SceneV1 &scene,
                             uint32_t expected_generation);
  SceneStoreResult delete_slot(uint8_t slot_one_based,
                               uint32_t expected_generation);
  SceneStoreResult replace_all(const led::SceneBank &bank,
                               uint32_t expected_generation,
                               bool recover_corrupt = false);

  const led::SceneBank &bank() const;
  uint32_t generation() const;
  SceneStoreHealth health() const;
  const SceneStoreDiagnostics &diagnostics() const;
  void observe_write_duration(uint32_t duration_us);

 private:
  struct BankView {
    SceneRecordStatus status;
    DecodedSceneRecord decoded;
  };

  BankView read_bank(uint8_t bank_index);
  void publish(const led::SceneBank &bank, uint32_t generation,
               int8_t active_bank, SceneStoreHealth health);
  SceneStoreResult result(SceneStoreResultCode code, bool no_change = false,
                          led::SceneValidationError validation_error =
                              led::SceneValidationError::None) const;
  SceneStoreResult commit(const led::SceneBank &candidate);
  SceneStoreResult recover_banks(const led::SceneBank &candidate);
  bool mutation_allowed(bool recovery) const;
  int8_t choose_write_bank() const;

  SceneRecordBackend &backend_;
  led::SceneCatalog &catalog_;
  led::SceneBank bank_;
  SceneStoreDiagnostics diagnostics_;
};

const char *scene_record_status_name(SceneRecordStatus status);
const char *scene_store_health_name(SceneStoreHealth health);
const char *scene_store_result_name(SceneStoreResultCode code);

} // namespace storage
