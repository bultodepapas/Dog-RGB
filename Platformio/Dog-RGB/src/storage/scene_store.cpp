#include "storage/scene_store.h"

#include <string.h>

#include "util/crc32.h"

namespace storage {
namespace {

static const uint8_t SCENE_MAGIC[4] = {'S', 'C', 'N', '1'};
static const size_t RECORD_SCENES_OFFSET = 16;
static const size_t RECORD_CRC_OFFSET = SCENE_RECORD_BYTES - 4U;

static void write_u16_le(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

static uint16_t read_u16_le(const uint8_t *in) {
  return static_cast<uint16_t>(in[0]) |
         (static_cast<uint16_t>(in[1]) << 8U);
}

static void write_u32_le(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

static uint32_t read_u32_le(const uint8_t *in) {
  return static_cast<uint32_t>(in[0]) |
         (static_cast<uint32_t>(in[1]) << 8U) |
         (static_cast<uint32_t>(in[2]) << 16U) |
         (static_cast<uint32_t>(in[3]) << 24U);
}

static bool all_zero(const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (data[i] != 0U) return false;
  }
  return true;
}

static uint32_t next_generation(uint32_t generation) {
  uint32_t next = generation + 1U;
  if (next == 0U) next = 1U;
  return next;
}

static bool canonical_bank(const led::SceneBank &input,
                           led::SceneBank &output,
                           led::SceneValidationError *error) {
  led::scene_bank_clear(output);
  if ((input.occupied_mask & 0xF0U) != 0U) {
    if (error != nullptr) *error = led::SceneValidationError::InvalidWire;
    return false;
  }
  output.occupied_mask = input.occupied_mask;
  for (uint8_t i = 0; i < led::SCENE_USER_SLOT_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1U << i);
    if ((input.occupied_mask & bit) == 0U) continue;
    if (input.slots[i].scene_id != led::scene_id_from_slot(i + 1U) ||
        !led::scene_validate(input.slots[i], error)) {
      if (error != nullptr && *error == led::SceneValidationError::None) {
        *error = led::SceneValidationError::InvalidId;
      }
      return false;
    }
    output.slots[i] = input.slots[i];
  }
  if (error != nullptr) *error = led::SceneValidationError::None;
  return true;
}

} // namespace

bool encode_scene_record(const led::SceneBank &bank, uint32_t generation,
                         uint8_t *out, size_t out_size) {
  if (out == nullptr || out_size != SCENE_RECORD_BYTES || generation == 0U) {
    return false;
  }
  led::SceneBank canonical = {};
  if (!canonical_bank(bank, canonical, nullptr)) return false;
  memset(out, 0, out_size);
  memcpy(out, SCENE_MAGIC, sizeof(SCENE_MAGIC));
  write_u16_le(out + 4, SCENE_RECORD_VERSION);
  write_u16_le(out + 6, static_cast<uint16_t>(SCENE_RECORD_BYTES));
  write_u32_le(out + 8, generation);
  out[12] = led::SCENE_SCHEMA_VERSION;
  out[13] = led::SCENE_USER_SLOT_COUNT;
  out[14] = canonical.occupied_mask;
  out[15] = 0;
  for (uint8_t i = 0; i < led::SCENE_USER_SLOT_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1U << i);
    uint8_t *scene_out = out + RECORD_SCENES_OFFSET +
                         static_cast<size_t>(i) * led::SCENE_WIRE_BYTES;
    if ((canonical.occupied_mask & bit) != 0U &&
        !led::scene_encode(canonical.slots[i], scene_out,
                           led::SCENE_WIRE_BYTES)) {
      return false;
    }
  }
  write_u32_le(out + RECORD_CRC_OFFSET,
               util::crc32_ieee(out, RECORD_CRC_OFFSET));
  return true;
}

SceneRecordStatus decode_scene_record(const uint8_t *data, size_t data_size,
                                      DecodedSceneRecord &out) {
  memset(&out, 0, sizeof(out));
  if (data == nullptr || data_size < 16U ||
      memcmp(data, SCENE_MAGIC, sizeof(SCENE_MAGIC)) != 0) {
    return SceneRecordStatus::Corrupt;
  }
  const uint16_t record_version = read_u16_le(data + 4);
  const uint16_t record_size = read_u16_le(data + 6);
  if (record_size != data_size || record_size < 16U ||
      data_size > SCENE_RECORD_READ_MAX) {
    return SceneRecordStatus::Corrupt;
  }
  const size_t crc_offset = data_size - 4U;
  if (read_u32_le(data + crc_offset) != util::crc32_ieee(data, crc_offset)) {
    return SceneRecordStatus::Corrupt;
  }
  out.record_version = record_version;
  out.generation = read_u32_le(data + 8);
  if (out.generation == 0U) return SceneRecordStatus::Corrupt;
  if (record_version > SCENE_RECORD_VERSION) return SceneRecordStatus::Future;
  if (record_version != SCENE_RECORD_VERSION ||
      data_size != SCENE_RECORD_BYTES || data[12] != led::SCENE_SCHEMA_VERSION ||
      data[13] != led::SCENE_USER_SLOT_COUNT || (data[14] & 0xF0U) != 0U ||
      data[15] != 0U) {
    return SceneRecordStatus::Corrupt;
  }

  led::scene_bank_clear(out.bank);
  out.bank.occupied_mask = data[14];
  for (uint8_t i = 0; i < led::SCENE_USER_SLOT_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1U << i);
    const uint8_t *scene_data = data + RECORD_SCENES_OFFSET +
                                static_cast<size_t>(i) * led::SCENE_WIRE_BYTES;
    if ((out.bank.occupied_mask & bit) == 0U) {
      if (!all_zero(scene_data, led::SCENE_WIRE_BYTES)) {
        return SceneRecordStatus::Corrupt;
      }
      continue;
    }
    if (!led::scene_decode(scene_data, led::SCENE_WIRE_BYTES,
                           out.bank.slots[i]) ||
        out.bank.slots[i].scene_id != led::scene_id_from_slot(i + 1U)) {
      return SceneRecordStatus::Corrupt;
    }
  }
  return SceneRecordStatus::Valid;
}

bool scene_generation_is_newer(uint32_t candidate, uint32_t reference) {
  const uint32_t distance = candidate - reference;
  return distance != 0U && distance < UINT32_C(0x80000000);
}

bool scene_generation_is_ambiguous(uint32_t left, uint32_t right) {
  return left != right && left - right == UINT32_C(0x80000000);
}

SceneStore::SceneStore(SceneRecordBackend &backend, led::SceneCatalog &catalog)
    : backend_(backend), catalog_(catalog), bank_{}, diagnostics_{} {
  led::scene_bank_clear(bank_);
  diagnostics_.health = SceneStoreHealth::Uninitialized;
  diagnostics_.bank_a = SceneRecordStatus::Missing;
  diagnostics_.bank_b = SceneRecordStatus::Missing;
  diagnostics_.active_bank = -1;
  diagnostics_.free_entries = 0;
}

SceneStore::BankView SceneStore::read_bank(uint8_t bank_index) {
  BankView view = {};
  view.status = SceneRecordStatus::IoError;
  uint8_t bytes[SCENE_RECORD_READ_MAX] = {};
  size_t actual_size = 0;
  const SceneBackendRead read =
      backend_.read(bank_index, bytes, sizeof(bytes), actual_size);
  if (read == SceneBackendRead::Missing) {
    view.status = SceneRecordStatus::Missing;
  } else if (read == SceneBackendRead::TooLarge) {
    view.status = SceneRecordStatus::Oversized;
  } else if (read == SceneBackendRead::Error) {
    view.status = SceneRecordStatus::IoError;
  } else {
    view.status = decode_scene_record(bytes, actual_size, view.decoded);
  }
  return view;
}

SceneStoreHealth SceneStore::load() {
  diagnostics_.load_count++;
  const BankView a = read_bank(0);
  const BankView b = read_bank(1);
  diagnostics_.bank_a = a.status;
  diagnostics_.bank_b = b.status;
  diagnostics_.bank_a_generation = a.decoded.generation;
  diagnostics_.bank_b_generation = b.decoded.generation;
  diagnostics_.free_entries = backend_.free_entries();

  if (a.status == SceneRecordStatus::Oversized ||
      b.status == SceneRecordStatus::Oversized) {
    publish(led::SceneBank{}, 0, -1, SceneStoreHealth::OversizedUnknown);
    return diagnostics_.health;
  }
  if (a.status == SceneRecordStatus::Future ||
      b.status == SceneRecordStatus::Future) {
    publish(led::SceneBank{}, 0, -1, SceneStoreHealth::ReadOnlyFuture);
    return diagnostics_.health;
  }

  const bool a_valid = a.status == SceneRecordStatus::Valid;
  const bool b_valid = b.status == SceneRecordStatus::Valid;
  if (a_valid && b_valid) {
    if (a.decoded.generation == b.decoded.generation) {
      if (!led::scene_bank_equal(a.decoded.bank, b.decoded.bank)) {
        publish(led::SceneBank{}, 0, -1, SceneStoreHealth::Ambiguous);
      } else {
        publish(a.decoded.bank, a.decoded.generation, 0,
                SceneStoreHealth::Healthy);
      }
      return diagnostics_.health;
    }
    if (scene_generation_is_ambiguous(a.decoded.generation,
                                      b.decoded.generation)) {
      publish(led::SceneBank{}, 0, -1, SceneStoreHealth::Ambiguous);
      return diagnostics_.health;
    }
    if (scene_generation_is_newer(a.decoded.generation,
                                  b.decoded.generation)) {
      publish(a.decoded.bank, a.decoded.generation, 0,
              SceneStoreHealth::Healthy);
    } else {
      publish(b.decoded.bank, b.decoded.generation, 1,
              SceneStoreHealth::Healthy);
    }
    return diagnostics_.health;
  }
  if (a_valid || b_valid) {
    const BankView &selected = a_valid ? a : b;
    publish(selected.decoded.bank, selected.decoded.generation,
            a_valid ? 0 : 1, SceneStoreHealth::Recovered);
    if ((a_valid ? b.status : a.status) != SceneRecordStatus::Missing) {
      diagnostics_.recovery_count++;
    }
    return diagnostics_.health;
  }

  if (a.status == SceneRecordStatus::Missing &&
      b.status == SceneRecordStatus::Missing) {
    publish(led::SceneBank{}, 0, -1, SceneStoreHealth::Empty);
  } else if ((a.status == SceneRecordStatus::Missing &&
              b.status == SceneRecordStatus::Corrupt) ||
             (b.status == SceneRecordStatus::Missing &&
              a.status == SceneRecordStatus::Corrupt)) {
    publish(led::SceneBank{}, 0, -1, SceneStoreHealth::DegradedEmpty);
    diagnostics_.recovery_count++;
  } else if (a.status == SceneRecordStatus::IoError ||
             b.status == SceneRecordStatus::IoError) {
    publish(led::SceneBank{}, 0, -1, SceneStoreHealth::Unavailable);
  } else {
    publish(led::SceneBank{}, 0, -1, SceneStoreHealth::Corrupt);
  }
  return diagnostics_.health;
}

SceneStoreResult SceneStore::save_slot(uint8_t slot_one_based,
                                       const led::SceneV1 &scene,
                                       uint32_t expected_generation) {
  if (slot_one_based < 1U ||
      slot_one_based > led::SCENE_USER_SLOT_COUNT) {
    return result(SceneStoreResultCode::InvalidSlot);
  }
  if (!mutation_allowed(false)) {
    const SceneStoreResultCode code =
        diagnostics_.health == SceneStoreHealth::Corrupt ||
                diagnostics_.health == SceneStoreHealth::Ambiguous
            ? SceneStoreResultCode::RecoveryRequired
            : (diagnostics_.health == SceneStoreHealth::Unavailable
                   ? SceneStoreResultCode::Unavailable
                   : SceneStoreResultCode::ReadOnly);
    return result(code);
  }
  if (expected_generation != diagnostics_.generation) {
    return result(SceneStoreResultCode::GenerationConflict);
  }
  led::SceneV1 candidate_scene = scene;
  candidate_scene.scene_id = led::scene_id_from_slot(slot_one_based);
  led::SceneValidationError validation_error =
      led::SceneValidationError::None;
  if (!led::scene_validate(candidate_scene, &validation_error)) {
    return result(SceneStoreResultCode::InvalidScene, false,
                  validation_error);
  }
  led::SceneBank candidate = bank_;
  const uint8_t index = slot_one_based - 1U;
  candidate.slots[index] = candidate_scene;
  candidate.occupied_mask |= static_cast<uint8_t>(1U << index);
  if (led::scene_bank_equal(candidate, bank_)) {
    return result(SceneStoreResultCode::NoChange, true);
  }
  return commit(candidate);
}

SceneStoreResult SceneStore::delete_slot(uint8_t slot_one_based,
                                         uint32_t expected_generation) {
  if (slot_one_based < 1U ||
      slot_one_based > led::SCENE_USER_SLOT_COUNT) {
    return result(SceneStoreResultCode::InvalidSlot);
  }
  if (!mutation_allowed(false)) {
    const SceneStoreResultCode code =
        diagnostics_.health == SceneStoreHealth::Unavailable
            ? SceneStoreResultCode::Unavailable
            : (diagnostics_.health == SceneStoreHealth::Corrupt ||
                       diagnostics_.health == SceneStoreHealth::Ambiguous
                   ? SceneStoreResultCode::RecoveryRequired
                   : SceneStoreResultCode::ReadOnly);
    return result(code);
  }
  if (expected_generation != diagnostics_.generation) {
    return result(SceneStoreResultCode::GenerationConflict);
  }
  const uint8_t index = slot_one_based - 1U;
  const uint8_t bit = static_cast<uint8_t>(1U << index);
  if ((bank_.occupied_mask & bit) == 0U) {
    return result(SceneStoreResultCode::NoChange, true);
  }
  led::SceneBank candidate = bank_;
  memset(&candidate.slots[index], 0, sizeof(candidate.slots[index]));
  candidate.occupied_mask &= static_cast<uint8_t>(~bit);
  return commit(candidate);
}

SceneStoreResult SceneStore::replace_all(const led::SceneBank &bank,
                                         uint32_t expected_generation,
                                         bool recover_corrupt) {
  if (!mutation_allowed(recover_corrupt)) {
    const SceneStoreResultCode code =
        diagnostics_.health == SceneStoreHealth::Unavailable
            ? SceneStoreResultCode::Unavailable
            : (diagnostics_.health == SceneStoreHealth::Corrupt ||
                       diagnostics_.health == SceneStoreHealth::Ambiguous
                   ? SceneStoreResultCode::RecoveryRequired
                   : SceneStoreResultCode::ReadOnly);
    return result(code);
  }
  if (expected_generation != diagnostics_.generation) {
    return result(SceneStoreResultCode::GenerationConflict);
  }
  led::SceneBank candidate = {};
  led::SceneValidationError validation_error =
      led::SceneValidationError::None;
  if (!canonical_bank(bank, candidate, &validation_error)) {
    return result(SceneStoreResultCode::InvalidScene, false,
                  validation_error);
  }
  const bool recovery_commit =
      recover_corrupt &&
      (diagnostics_.health == SceneStoreHealth::Corrupt ||
       diagnostics_.health == SceneStoreHealth::Ambiguous);
  if (!recovery_commit && led::scene_bank_equal(candidate, bank_)) {
    return result(SceneStoreResultCode::NoChange, true);
  }
  return recovery_commit ? recover_banks(candidate) : commit(candidate);
}

const led::SceneBank &SceneStore::bank() const {
  return bank_;
}

uint32_t SceneStore::generation() const {
  return diagnostics_.generation;
}

SceneStoreHealth SceneStore::health() const {
  return diagnostics_.health;
}

const SceneStoreDiagnostics &SceneStore::diagnostics() const {
  return diagnostics_;
}

void SceneStore::observe_write_duration(uint32_t duration_us) {
  diagnostics_.last_write_us = duration_us;
  if (duration_us > diagnostics_.max_write_us) {
    diagnostics_.max_write_us = duration_us;
  }
}

void SceneStore::publish(const led::SceneBank &bank, uint32_t generation,
                         int8_t active_bank, SceneStoreHealth health) {
  bank_ = bank;
  diagnostics_.generation = generation;
  diagnostics_.active_bank = active_bank;
  diagnostics_.health = health;
  if (generation == 0U) {
    catalog_.clear_users(0);
  } else if (!catalog_.replace_users(bank_, generation)) {
    led::scene_bank_clear(bank_);
    diagnostics_.generation = 0;
    diagnostics_.active_bank = -1;
    diagnostics_.health = SceneStoreHealth::Uncertain;
    catalog_.clear_users(0);
  }
}

SceneStoreResult SceneStore::result(
    SceneStoreResultCode code, bool no_change,
    led::SceneValidationError validation_error) const {
  return SceneStoreResult{code, no_change, diagnostics_.generation,
                          diagnostics_.health, validation_error};
}

SceneStoreResult SceneStore::commit(const led::SceneBank &candidate) {
  const uint32_t candidate_generation =
      next_generation(diagnostics_.generation);
  const int8_t target_bank = choose_write_bank();
  if (target_bank < 0) return result(SceneStoreResultCode::ReadOnly);

  uint8_t encoded[SCENE_RECORD_BYTES] = {};
  if (!encode_scene_record(candidate, candidate_generation, encoded,
                           sizeof(encoded))) {
    return result(SceneStoreResultCode::InvalidScene);
  }
  size_t written_size = 0;
  const SceneBackendWrite write_status =
      backend_.write(static_cast<uint8_t>(target_bank), encoded,
                     sizeof(encoded), written_size);
  if (write_status != SceneBackendWrite::Ok ||
      written_size != sizeof(encoded)) {
    diagnostics_.write_failures++;
  }

  const BankView verified = read_bank(static_cast<uint8_t>(target_bank));
  const bool candidate_verified =
      verified.status == SceneRecordStatus::Valid &&
      verified.decoded.generation == candidate_generation &&
      led::scene_bank_equal(verified.decoded.bank, candidate);
  if (candidate_verified) {
    if (target_bank == 0) {
      diagnostics_.bank_a = SceneRecordStatus::Valid;
      diagnostics_.bank_a_generation = candidate_generation;
    } else {
      diagnostics_.bank_b = SceneRecordStatus::Valid;
      diagnostics_.bank_b_generation = candidate_generation;
    }
    const SceneRecordStatus other = target_bank == 0 ? diagnostics_.bank_b
                                                      : diagnostics_.bank_a;
    publish(candidate, candidate_generation, target_bank,
            other == SceneRecordStatus::Valid ? SceneStoreHealth::Healthy
                                               : SceneStoreHealth::Recovered);
    diagnostics_.mutation_count++;
    diagnostics_.free_entries = backend_.free_entries();
    return result(SceneStoreResultCode::Ok);
  }

  diagnostics_.verify_failures++;
  load();
  if (diagnostics_.generation == candidate_generation &&
      led::scene_bank_equal(bank_, candidate)) {
    diagnostics_.mutation_count++;
    return result(SceneStoreResultCode::Ok);
  }
  if (diagnostics_.health == SceneStoreHealth::Ambiguous ||
      diagnostics_.health == SceneStoreHealth::Unavailable ||
      diagnostics_.health == SceneStoreHealth::Uncertain) {
    diagnostics_.uncertain_commits++;
    diagnostics_.health = SceneStoreHealth::Uncertain;
    return result(SceneStoreResultCode::Uncertain);
  }
  if (write_status == SceneBackendWrite::Full) {
    return result(SceneStoreResultCode::StorageFull);
  }
  return result(write_status == SceneBackendWrite::Ok
                    ? SceneStoreResultCode::VerifyFailed
                    : SceneStoreResultCode::WriteFailed);
}

SceneStoreResult SceneStore::recover_banks(
    const led::SceneBank &candidate) {
  // Ambiguous serial numbers cannot be repaired durably by replacing only one
  // side: the surviving record may still win (or remain exactly half a range
  // away) on reboot. Recovery is an explicit administrative operation, so it
  // writes the same verified epoch to both banks and publishes only afterward.
  static const uint32_t RECOVERY_GENERATION = 1U;
  uint8_t encoded[SCENE_RECORD_BYTES] = {};
  if (!encode_scene_record(candidate, RECOVERY_GENERATION, encoded,
                           sizeof(encoded))) {
    return result(SceneStoreResultCode::InvalidScene);
  }

  bool any_write_failure = false;
  for (uint8_t bank_index = 0; bank_index < 2U; ++bank_index) {
    size_t written_size = 0;
    const SceneBackendWrite write_status =
        backend_.write(bank_index, encoded, sizeof(encoded), written_size);
    if (write_status != SceneBackendWrite::Ok ||
        written_size != sizeof(encoded)) {
      diagnostics_.write_failures++;
      any_write_failure = true;
    }
    const BankView verified = read_bank(bank_index);
    if (verified.status != SceneRecordStatus::Valid ||
        verified.decoded.generation != RECOVERY_GENERATION ||
        !led::scene_bank_equal(verified.decoded.bank, candidate)) {
      diagnostics_.verify_failures++;
      load();
      if (diagnostics_.health == SceneStoreHealth::Ambiguous ||
          diagnostics_.health == SceneStoreHealth::Unavailable ||
          diagnostics_.health == SceneStoreHealth::Uncertain) {
        diagnostics_.uncertain_commits++;
        diagnostics_.health = SceneStoreHealth::Uncertain;
        return result(SceneStoreResultCode::Uncertain);
      }
      if (write_status == SceneBackendWrite::Full) {
        return result(SceneStoreResultCode::StorageFull);
      }
      return result(any_write_failure ? SceneStoreResultCode::WriteFailed
                                      : SceneStoreResultCode::VerifyFailed);
    }
  }

  diagnostics_.bank_a = SceneRecordStatus::Valid;
  diagnostics_.bank_b = SceneRecordStatus::Valid;
  diagnostics_.bank_a_generation = RECOVERY_GENERATION;
  diagnostics_.bank_b_generation = RECOVERY_GENERATION;
  publish(candidate, RECOVERY_GENERATION, 0, SceneStoreHealth::Healthy);
  diagnostics_.mutation_count++;
  diagnostics_.free_entries = backend_.free_entries();
  return result(SceneStoreResultCode::Ok);
}

bool SceneStore::mutation_allowed(bool recovery) const {
  switch (diagnostics_.health) {
    case SceneStoreHealth::Empty:
    case SceneStoreHealth::Healthy:
    case SceneStoreHealth::Recovered:
    case SceneStoreHealth::DegradedEmpty:
      return true;
    case SceneStoreHealth::Corrupt:
    case SceneStoreHealth::Ambiguous:
      return recovery;
    default:
      return false;
  }
}

int8_t SceneStore::choose_write_bank() const {
  if (diagnostics_.active_bank == 0) return 1;
  if (diagnostics_.active_bank == 1) return 0;
  if (diagnostics_.health == SceneStoreHealth::DegradedEmpty) {
    if (diagnostics_.bank_a == SceneRecordStatus::Corrupt &&
        diagnostics_.bank_b == SceneRecordStatus::Missing) {
      return 0;
    }
    if (diagnostics_.bank_b == SceneRecordStatus::Corrupt &&
        diagnostics_.bank_a == SceneRecordStatus::Missing) {
      return 1;
    }
  }
  return 0;
}

const char *scene_record_status_name(SceneRecordStatus status) {
  switch (status) {
    case SceneRecordStatus::Missing: return "missing";
    case SceneRecordStatus::Valid: return "valid";
    case SceneRecordStatus::Corrupt: return "corrupt";
    case SceneRecordStatus::Future: return "future";
    case SceneRecordStatus::Oversized: return "oversized";
    case SceneRecordStatus::IoError: return "io_error";
  }
  return "unknown";
}

const char *scene_store_health_name(SceneStoreHealth health) {
  switch (health) {
    case SceneStoreHealth::Uninitialized: return "uninitialized";
    case SceneStoreHealth::Empty: return "empty";
    case SceneStoreHealth::Healthy: return "healthy";
    case SceneStoreHealth::Recovered: return "recovered";
    case SceneStoreHealth::DegradedEmpty: return "degraded_empty";
    case SceneStoreHealth::Corrupt: return "corrupt";
    case SceneStoreHealth::Ambiguous: return "ambiguous";
    case SceneStoreHealth::ReadOnlyFuture: return "read_only_future";
    case SceneStoreHealth::OversizedUnknown: return "oversized_unknown";
    case SceneStoreHealth::Unavailable: return "unavailable";
    case SceneStoreHealth::Uncertain: return "uncertain";
  }
  return "unknown";
}

const char *scene_store_result_name(SceneStoreResultCode code) {
  switch (code) {
    case SceneStoreResultCode::Ok: return "ok";
    case SceneStoreResultCode::NoChange: return "no_change";
    case SceneStoreResultCode::InvalidSlot: return "invalid_slot";
    case SceneStoreResultCode::InvalidScene: return "invalid_scene";
    case SceneStoreResultCode::GenerationConflict:
      return "generation_conflict";
    case SceneStoreResultCode::RecoveryRequired: return "recovery_required";
    case SceneStoreResultCode::ReadOnly: return "store_read_only";
    case SceneStoreResultCode::Unavailable: return "storage_unavailable";
    case SceneStoreResultCode::StorageFull: return "storage_full";
    case SceneStoreResultCode::WriteFailed: return "storage_write_failed";
    case SceneStoreResultCode::VerifyFailed: return "storage_verify_failed";
    case SceneStoreResultCode::Uncertain: return "storage_uncertain";
  }
  return "unknown";
}

} // namespace storage
