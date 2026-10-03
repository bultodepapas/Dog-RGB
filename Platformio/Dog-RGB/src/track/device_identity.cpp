#include "track/device_identity.h"

#include <string.h>

#include "util/crc32.h"

namespace track {
namespace identity {
namespace {

static const uint8_t kRecordMagic[4] = {'D', '3', 'I', 'D'};
static const uint16_t kRecordVersion = 1U;
static const size_t kRecordCrcOffset = 36U;

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

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        size_t size) {
  return memcmp(left, right, size) == 0;
}

static bool uuid_equal(const Uuid &left, const Uuid &right) {
  return bytes_equal(left.bytes, right.bytes, sizeof(left.bytes));
}

static bool ranges_overlap(const void *left, size_t left_size,
                           const void *right, size_t right_size) {
  if (left == nullptr || right == nullptr || left_size == 0U ||
      right_size == 0U) {
    return false;
  }
  const uintptr_t left_start = reinterpret_cast<uintptr_t>(left);
  const uintptr_t right_start = reinterpret_cast<uintptr_t>(right);
  const uintptr_t max_value = static_cast<uintptr_t>(-1);
  if (left_size > max_value - left_start ||
      right_size > max_value - right_start) {
    return true;
  }
  const uintptr_t left_end = left_start + left_size;
  const uintptr_t right_end = right_start + right_size;
  return left_start < right_end && right_start < left_end;
}

static bool records_equal(const Record &left, const Record &right) {
  return left.generation == right.generation &&
         left.last_boot == right.last_boot &&
         uuid_equal(left.device_id, right.device_id);
}

}  // namespace

bool uuid_is_nil(const Uuid &uuid) {
  for (size_t i = 0; i < sizeof(uuid.bytes); ++i) {
    if (uuid.bytes[i] != 0U) return false;
  }
  return true;
}

bool encode_record(const Record &record, uint8_t *out, size_t out_capacity) {
  if (out == nullptr || out_capacity < kRecordSize ||
      record.generation != record.last_boot ||
      uuid_is_nil(record.device_id) ||
      ranges_overlap(&record, sizeof(record), out, kRecordSize)) {
    return false;
  }

  uint8_t encoded[kRecordSize] = {};
  memcpy(encoded, kRecordMagic, sizeof(kRecordMagic));
  write_u16_le(encoded + 4U, kRecordVersion);
  write_u16_le(encoded + 6U, static_cast<uint16_t>(kRecordSize));
  write_u32_le(encoded + 8U, record.generation);
  write_u32_le(encoded + 12U, record.last_boot);
  memcpy(encoded + 16U, record.device_id.bytes,
         sizeof(record.device_id.bytes));
  write_u32_le(encoded + 32U, 0U);
  write_u32_le(encoded + kRecordCrcOffset,
               util::crc32_ieee(encoded, kRecordCrcOffset));
  memcpy(out, encoded, sizeof(encoded));
  return true;
}

RecordDecodeStatus decode_record(const uint8_t *data, size_t data_size,
                                 Record &out) {
  if (data == nullptr || data_size != kRecordSize ||
      ranges_overlap(data, data_size, &out, sizeof(out))) {
    return RecordDecodeStatus::kInvalid;
  }
  if (!bytes_equal(data, kRecordMagic, sizeof(kRecordMagic)) ||
      read_u16_le(data + 6U) != kRecordSize) {
    return RecordDecodeStatus::kInvalid;
  }

  const uint16_t version = read_u16_le(data + 4U);
  if (version > kRecordVersion) return RecordDecodeStatus::kFutureVersion;
  if (version != kRecordVersion || read_u32_le(data + 32U) != 0U ||
      read_u32_le(data + kRecordCrcOffset) !=
          util::crc32_ieee(data, kRecordCrcOffset)) {
    return RecordDecodeStatus::kInvalid;
  }

  Record decoded = {};
  decoded.generation = read_u32_le(data + 8U);
  decoded.last_boot = read_u32_le(data + 12U);
  memcpy(decoded.device_id.bytes, data + 16U,
         sizeof(decoded.device_id.bytes));
  if (decoded.generation != decoded.last_boot ||
      uuid_is_nil(decoded.device_id)) {
    return RecordDecodeStatus::kInvalid;
  }
  out = decoded;
  return RecordDecodeStatus::kValid;
}

SequenceCounter::SequenceCounter(uint32_t first) : next_(first) {}

bool SequenceCounter::reserve(uint32_t count, uint32_t &first) {
  const uint64_t limit = static_cast<uint64_t>(UINT32_MAX) + 1ULL;
  if (count == 0U || next_ > limit ||
      static_cast<uint64_t>(count) > limit - next_) {
    return false;
  }
  const uint32_t reserved_first = static_cast<uint32_t>(next_);
  next_ += static_cast<uint64_t>(count);
  first = reserved_first;
  return true;
}

uint64_t SequenceCounter::next() const { return next_; }

DeviceIdentityStore::DeviceIdentityStore(Backend &backend)
    : backend_(backend), mounted_(false), provisioned_(false),
      session_active_(false), faulted_(false), status_(Status::kNotMounted),
      device_id_{}, current_boot_sequence_(0U), bank_generations_{0U, 0U},
      current_bank_(-1), chunk_counter_(0U), point_counter_(0U) {}

DeviceIdentityStore::BankView DeviceIdentityStore::read_bank(uint8_t slot) {
  BankView view = {};
  view.read_status = ReadStatus::kError;
  view.decode_status = RecordDecodeStatus::kInvalid;
  view.actual_size = 0U;
  const ReadStatus read_status =
      backend_.read(slot, view.bytes, sizeof(view.bytes), view.actual_size);
  view.read_status = read_status;
  if (read_status == ReadStatus::kOk &&
      view.actual_size <= sizeof(view.bytes)) {
    view.decode_status =
        decode_record(view.bytes, view.actual_size, view.record);
  }
  return view;
}

DeviceIdentityStore::PairView DeviceIdentityStore::read_pair() {
  PairView pair = {};
  pair.banks[kBankA] = read_bank(kBankA);
  pair.banks[kBankB] = read_bank(kBankB);
  pair.status = classify_pair(pair);
  return pair;
}

Status DeviceIdentityStore::classify_pair(PairView &pair) const {
  const BankView &a = pair.banks[kBankA];
  const BankView &b = pair.banks[kBankB];

  if (a.read_status == ReadStatus::kError ||
      b.read_status == ReadStatus::kError) {
    return Status::kStorageError;
  }
  if (a.read_status == ReadStatus::kTooLarge ||
      b.read_status == ReadStatus::kTooLarge ||
      (a.read_status == ReadStatus::kOk && a.actual_size > kRecordSize) ||
      (b.read_status == ReadStatus::kOk && b.actual_size > kRecordSize)) {
    return Status::kOversized;
  }

  const bool a_missing = a.read_status == ReadStatus::kMissing;
  const bool b_missing = b.read_status == ReadStatus::kMissing;
  if (a_missing && b_missing) return Status::kEmpty;
  if (a_missing || b_missing) return Status::kCorrupt;

  if (a.read_status != ReadStatus::kOk || b.read_status != ReadStatus::kOk) {
    return Status::kStorageError;
  }
  if (a.decode_status == RecordDecodeStatus::kFutureVersion ||
      b.decode_status == RecordDecodeStatus::kFutureVersion) {
    return Status::kFutureVersion;
  }
  if (a.decode_status != RecordDecodeStatus::kValid ||
      b.decode_status != RecordDecodeStatus::kValid) {
    return Status::kCorrupt;
  }
  if (!uuid_equal(a.record.device_id, b.record.device_id)) {
    return Status::kUuidMismatch;
  }

  if (a.record.generation == 0U && b.record.generation == 0U) {
    // Both banks are the genesis state. Treat B as the logical current bank
    // so the first boot write deterministically lands in A.
    pair.current_bank = static_cast<int8_t>(kBankB);
    pair.current_generation = 0U;
    pair.device_id = a.record.device_id;
    return Status::kOk;
  }
  if (a.record.generation == b.record.generation) {
    return Status::kAmbiguous;
  }
  const uint32_t higher = a.record.generation > b.record.generation
                              ? a.record.generation
                              : b.record.generation;
  const uint32_t lower = a.record.generation < b.record.generation
                             ? a.record.generation
                             : b.record.generation;
  if (higher - lower != 1U) return Status::kGenerationMismatch;

  pair.current_bank = static_cast<int8_t>(
      a.record.generation > b.record.generation ? kBankA : kBankB);
  pair.current_generation = higher;
  pair.device_id = a.record.device_id;
  return Status::kOk;
}

bool DeviceIdentityStore::pair_matches(const PairView &pair, uint8_t bank_a,
                                       uint32_t generation_a,
                                       uint8_t bank_b,
                                       uint32_t generation_b,
                                       const Uuid &device_id) const {
  if (pair.status != Status::kOk || bank_a != kBankA || bank_b != kBankB ||
      !uuid_equal(pair.device_id, device_id)) {
    return false;
  }
  const Record &record_a = pair.banks[kBankA].record;
  const Record &record_b = pair.banks[kBankB].record;
  return record_a.generation == generation_a &&
         record_b.generation == generation_b &&
         uuid_equal(record_a.device_id, device_id) &&
         uuid_equal(record_b.device_id, device_id);
}

bool DeviceIdentityStore::write_record_verified(
    uint8_t slot, const Record &record, const uint8_t *expected_bytes) {
  if (expected_bytes == nullptr || slot > kBankB ||
      !backend_.write(slot, expected_bytes, kRecordSize)) {
    return false;
  }

  uint8_t readback[kRecordSize] = {};
  size_t actual_size = 0U;
  const ReadStatus read_status =
      backend_.read(slot, readback, sizeof(readback), actual_size);
  if (read_status != ReadStatus::kOk || actual_size != kRecordSize ||
      !bytes_equal(readback, expected_bytes, kRecordSize)) {
    return false;
  }
  Record decoded = {};
  return decode_record(readback, actual_size, decoded) ==
             RecordDecodeStatus::kValid &&
         records_equal(decoded, record);
}

void DeviceIdentityStore::latch_fault() {
  faulted_ = true;
  status_ = Status::kFaulted;
}

Status DeviceIdentityStore::mount() {
  if (faulted_) return Status::kFaulted;
  if (session_active_) return Status::kAlreadyActive;
  if (mounted_) return status_;
  mounted_ = true;

  const PairView pair = read_pair();
  status_ = pair.status;
  if (pair.status != Status::kOk) return status_;

  provisioned_ = true;
  device_id_ = pair.device_id;
  current_boot_sequence_ = pair.current_generation;
  current_bank_ = pair.current_bank;
  bank_generations_[kBankA] = pair.banks[kBankA].record.generation;
  bank_generations_[kBankB] = pair.banks[kBankB].record.generation;
  return Status::kOk;
}

Status DeviceIdentityStore::load() { return mount(); }

Status DeviceIdentityStore::provision(const Uuid &device_id) {
  if (faulted_) return Status::kFaulted;
  if (!mounted_) return Status::kNotMounted;
  if (provisioned_) return Status::kAlreadyProvisioned;
  if (status_ != Status::kEmpty) return status_;
  if (uuid_is_nil(device_id)) return Status::kInvalidArgument;

  // Recheck both slots immediately before writing. A caller must never use a
  // stale Empty observation to overwrite a newly populated bank.
  const PairView before = read_pair();
  if (before.status != Status::kEmpty) {
    latch_fault();
    return Status::kFaulted;
  }

  Record genesis = {};
  genesis.generation = 0U;
  genesis.last_boot = 0U;
  genesis.device_id = device_id;
  uint8_t bytes[kRecordSize] = {};
  if (!encode_record(genesis, bytes, sizeof(bytes))) {
    return Status::kInvalidArgument;
  }

  if (!write_record_verified(kBankA, genesis, bytes) ||
      !write_record_verified(kBankB, genesis, bytes)) {
    latch_fault();
    return Status::kFaulted;
  }
  const PairView pair = read_pair();
  if (!pair_matches(pair, kBankA, 0U, kBankB, 0U, device_id) ||
      !bytes_equal(pair.banks[kBankA].bytes, bytes, kRecordSize) ||
      !bytes_equal(pair.banks[kBankB].bytes, bytes, kRecordSize)) {
    latch_fault();
    return Status::kFaulted;
  }

  provisioned_ = true;
  status_ = Status::kOk;
  device_id_ = device_id;
  current_boot_sequence_ = 0U;
  current_bank_ = static_cast<int8_t>(kBankB);
  bank_generations_[kBankA] = 0U;
  bank_generations_[kBankB] = 0U;
  return Status::kOk;
}

Status DeviceIdentityStore::alloc_boot_sequence(BootSession &out) {
  if (faulted_) return Status::kFaulted;
  if (!mounted_) return Status::kNotMounted;
  if (!provisioned_) {
    return status_ == Status::kEmpty ? Status::kNotProvisioned : status_;
  }
  if (session_active_) return Status::kAlreadyActive;

  const PairView before = read_pair();
  if (!pair_matches(before, kBankA, bank_generations_[kBankA], kBankB,
                    bank_generations_[kBankB], device_id_) ||
      before.current_generation != current_boot_sequence_ ||
      before.current_bank != current_bank_) {
    latch_fault();
    return Status::kFaulted;
  }
  if (current_boot_sequence_ == UINT32_MAX) {
    return Status::kSequenceExhausted;
  }

  const uint32_t next_boot = current_boot_sequence_ + 1U;
  const uint8_t target_bank = static_cast<uint8_t>(current_bank_ == kBankA
                                                       ? kBankB
                                                       : kBankA);
  Record record = {};
  record.generation = next_boot;
  record.last_boot = next_boot;
  record.device_id = device_id_;
  uint8_t bytes[kRecordSize] = {};
  if (!encode_record(record, bytes, sizeof(bytes)) ||
      !write_record_verified(target_bank, record, bytes)) {
    latch_fault();
    return Status::kFaulted;
  }

  const PairView after = read_pair();
  const uint32_t expected_a = target_bank == kBankA
                                  ? next_boot
                                  : bank_generations_[kBankA];
  const uint32_t expected_b = target_bank == kBankB
                                  ? next_boot
                                  : bank_generations_[kBankB];
  if (!pair_matches(after, kBankA, expected_a, kBankB, expected_b,
                    device_id_) ||
      after.current_generation != next_boot ||
      after.current_bank != static_cast<int8_t>(target_bank) ||
      !bytes_equal(after.banks[target_bank].bytes, bytes, kRecordSize)) {
    latch_fault();
    return Status::kFaulted;
  }

  bank_generations_[target_bank] = next_boot;
  current_bank_ = static_cast<int8_t>(target_bank);
  current_boot_sequence_ = next_boot;
  chunk_counter_ = SequenceCounter(0U);
  point_counter_ = SequenceCounter(0U);
  session_active_ = true;
  status_ = Status::kOk;

  BootSession granted = {};
  granted.device_id = device_id_;
  granted.boot_sequence = next_boot;
  out = granted;
  return Status::kOk;
}

Status DeviceIdentityStore::reserve_chunk(uint32_t point_count,
                                          ChunkReservation &out) {
  if (faulted_) return Status::kFaulted;
  if (!mounted_) return Status::kNotMounted;
  if (!provisioned_) {
    return status_ == Status::kEmpty ? Status::kNotProvisioned : status_;
  }
  if (!session_active_) return Status::kNoActiveSession;
  if (point_count == 0U || point_count > kMaxPointCountPerChunk) {
    return Status::kInvalidArgument;
  }

  SequenceCounter next_chunks = chunk_counter_;
  SequenceCounter next_points = point_counter_;
  uint32_t chunk_sequence = 0U;
  uint32_t first_point_sequence = 0U;
  if (!next_chunks.reserve(1U, chunk_sequence) ||
      !next_points.reserve(point_count, first_point_sequence)) {
    return Status::kSequenceExhausted;
  }

  ChunkReservation reserved = {};
  reserved.device_id = device_id_;
  reserved.boot_sequence = current_boot_sequence_;
  reserved.chunk_sequence = chunk_sequence;
  reserved.first_point_sequence = first_point_sequence;
  reserved.point_count = point_count;

  chunk_counter_ = next_chunks;
  point_counter_ = next_points;
  out = reserved;
  return Status::kOk;
}

Status DeviceIdentityStore::status() const {
  return faulted_ ? Status::kFaulted : status_;
}

bool DeviceIdentityStore::mounted() const { return mounted_; }

bool DeviceIdentityStore::provisioned() const { return provisioned_; }

bool DeviceIdentityStore::session_active() const { return session_active_; }

uint32_t DeviceIdentityStore::current_boot_sequence() const {
  return current_boot_sequence_;
}

}  // namespace identity
}  // namespace track
