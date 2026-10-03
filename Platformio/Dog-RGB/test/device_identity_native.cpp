#include "track/device_identity.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using track::identity::Backend;
using track::identity::BootSession;
using track::identity::ChunkReservation;
using track::identity::DeviceIdentityStore;
using track::identity::Record;
using track::identity::RecordDecodeStatus;
using track::identity::ReadStatus;
using track::identity::SequenceCounter;
using track::identity::Status;
using track::identity::Uuid;
using track::identity::kMaxPointCountPerChunk;
using track::identity::kRecordSize;

unsigned long check_count = 0;

void check(bool value, const char *expression, int line) {
  ++check_count;
  if (!value) {
    std::cerr << "line " << line << ": " << expression << '\n';
    std::exit(1);
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

uint32_t crc32_ieee(const uint8_t *bytes, size_t size) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(
          -static_cast<int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void store_le32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
  out[2] = static_cast<uint8_t>(value >> 16U);
  out[3] = static_cast<uint8_t>(value >> 24U);
}

Uuid make_uuid(uint8_t first) {
  Uuid id = {};
  for (size_t i = 0; i < sizeof(id.bytes); ++i) {
    id.bytes[i] = static_cast<uint8_t>(first + i);
  }
  return id;
}

bool same_uuid(const Uuid &left, const Uuid &right) {
  return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

std::vector<uint8_t> encode(const Record &record) {
  std::vector<uint8_t> bytes(kRecordSize, 0xA5U);
  CHECK(track::identity::encode_record(record, bytes.data(), bytes.size()));
  return bytes;
}

Record make_record(uint32_t generation, const Uuid &id) {
  Record record = {};
  record.generation = generation;
  record.last_boot = generation;
  record.device_id = id;
  return record;
}

void put_record(Backend &backend, uint8_t slot, const Record &record);

struct Bank {
  bool present;
  std::vector<uint8_t> bytes;
  Bank() : present(false), bytes() {}
};

enum class WriteFault {
  kNone,
  kFailBefore,
  kPrefixThenFail,
  kFullThenFail,
};

class MemoryBackend : public Backend {
 public:
  Bank banks[2];
  unsigned reads;
  unsigned writes;
  int error_slot;
  int write_fault_slot;
  int fail_read_after_write;
  int short_read_after_write;
  unsigned reads_after_write;
  WriteFault write_fault;
  size_t write_prefix;
  std::vector<uint8_t> last_write;
  uint8_t last_write_slot;
  bool last_write_readback_exact;
  std::vector<std::string> events;

  MemoryBackend()
      : banks(), reads(0), writes(0), error_slot(-1),
        write_fault_slot(-1),
        fail_read_after_write(-1), short_read_after_write(-1),
        reads_after_write(0), write_fault(WriteFault::kNone),
        write_prefix(0), last_write(), last_write_slot(0),
        last_write_readback_exact(false), events(), completed_write_(false) {}

  ReadStatus read(uint8_t slot, uint8_t *out, size_t capacity,
                  size_t &actual_size) override {
    ++reads;
    events.push_back(std::string("r") + static_cast<char>('0' + slot));
    actual_size = 0;
    if (slot >= 2U || out == nullptr) return ReadStatus::kError;
    if (completed_write_) {
      ++reads_after_write;
      if (reads_after_write == static_cast<unsigned>(fail_read_after_write)) {
        return ReadStatus::kError;
      }
    }
    if (static_cast<int>(slot) == error_slot) return ReadStatus::kError;
    const Bank &bank = banks[slot];
    if (!bank.present) return ReadStatus::kMissing;
    actual_size = bank.bytes.size();
    if (bank.bytes.size() > capacity) return ReadStatus::kTooLarge;
    size_t copied = bank.bytes.size();
    if (completed_write_ &&
        reads_after_write == static_cast<unsigned>(short_read_after_write) &&
        copied != 0U) {
      --copied;
      actual_size = copied;
    }
    if (copied != 0U) std::memcpy(out, bank.bytes.data(), copied);
    if (completed_write_ && slot == last_write_slot &&
        bank.bytes == last_write && actual_size == last_write.size()) {
      last_write_readback_exact = true;
    }
    return ReadStatus::kOk;
  }

  bool write(uint8_t slot, const uint8_t *data, size_t size) override {
    ++writes;
    events.push_back(std::string("w") + static_cast<char>('0' + slot));
    if (slot >= 2U || data == nullptr) return false;
    const std::vector<uint8_t> next(data, data + size);
    last_write = next;
    last_write_slot = slot;
    last_write_readback_exact = false;
    reads_after_write = 0;
    completed_write_ = false;
    const bool inject_fault = write_fault != WriteFault::kNone &&
                              (write_fault_slot < 0 ||
                               write_fault_slot == static_cast<int>(slot));
    if (inject_fault && write_fault == WriteFault::kFailBefore) {
      write_fault = WriteFault::kNone;
      write_fault_slot = -1;
      return false;
    }
    if (inject_fault && write_fault == WriteFault::kPrefixThenFail) {
      Bank &bank = banks[slot];
      bank.present = true;
      if (bank.bytes.size() < size) bank.bytes.resize(size, 0xFFU);
      const size_t prefix = write_prefix < size ? write_prefix : size;
      if (prefix != 0U) std::memcpy(bank.bytes.data(), data, prefix);
      write_fault = WriteFault::kNone;
      write_fault_slot = -1;
      return false;
    }
    banks[slot].present = true;
    banks[slot].bytes.assign(data, data + size);
    completed_write_ = true;
    if (inject_fault && write_fault == WriteFault::kFullThenFail) {
      write_fault = WriteFault::kNone;
      write_fault_slot = -1;
      return false;
    }
    return true;
  }

  void set_write_fault(WriteFault fault, size_t prefix = 0U,
                       int slot = -1) {
    write_fault = fault;
    write_prefix = prefix;
    write_fault_slot = slot;
  }

 private:
  bool completed_write_;
};

void put_record(Backend &backend, uint8_t slot, const Record &record) {
  const std::vector<uint8_t> bytes = encode(record);
  CHECK(backend.write(slot, bytes.data(), bytes.size()));
}

void set_crc(std::vector<uint8_t> &bytes) {
  CHECK(bytes.size() == kRecordSize);
  store_le32(bytes.data() + 36U, crc32_ieee(bytes.data(), 36U));
}

void seed_boot_one(MemoryBackend &backend, const Uuid &id) {
  DeviceIdentityStore store(backend);
  CHECK(store.mount() == Status::kEmpty);
  CHECK(store.provision(id) == Status::kOk);
  BootSession first = {};
  CHECK(store.alloc_boot_sequence(first) == Status::kOk);
  CHECK(first.boot_sequence == 1U && same_uuid(first.device_id, id));
}

void test_record_codec_and_golden() {
  Record record = make_record(0x01020304U, Uuid());
  const uint8_t golden_uuid[16] = {
      0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
      0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU};
  std::memcpy(record.device_id.bytes, golden_uuid, sizeof(golden_uuid));
  const uint8_t expected[kRecordSize] = {
      0x44U, 0x33U, 0x49U, 0x44U, 0x01U, 0x00U, 0x28U, 0x00U,
      0x04U, 0x03U, 0x02U, 0x01U, 0x04U, 0x03U, 0x02U, 0x01U,
      0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
      0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU,
      0x00U, 0x00U, 0x00U, 0x00U, 0xCDU, 0xB1U, 0x00U, 0x4DU};
  uint8_t bytes[kRecordSize];
  std::memset(bytes, 0xA5, sizeof(bytes));
  CHECK(track::identity::encode_record(record, bytes, sizeof(bytes)));
  CHECK(std::memcmp(bytes, expected, sizeof(bytes)) == 0);
  CHECK(crc32_ieee(bytes, 36U) == 0x4D00B1CDU);

  Record decoded = {};
  decoded.generation = 99U;
  CHECK(track::identity::decode_record(expected, sizeof(expected), decoded) ==
        RecordDecodeStatus::kValid);
  CHECK(decoded.generation == record.generation);
  CHECK(decoded.last_boot == record.last_boot);
  CHECK(same_uuid(decoded.device_id, record.device_id));

  std::vector<uint8_t> future(expected, expected + sizeof(expected));
  future[4] = 2U;
  set_crc(future);
  Record sentinel = make_record(77U, make_uuid(0x80U));
  const Record before = sentinel;
  CHECK(track::identity::decode_record(future.data(), future.size(), sentinel) ==
        RecordDecodeStatus::kFutureVersion);
  CHECK(sentinel.generation == before.generation &&
        sentinel.last_boot == before.last_boot &&
        same_uuid(sentinel.device_id, before.device_id));

  std::vector<uint8_t> bad_crc(expected, expected + sizeof(expected));
  bad_crc[36] ^= 0x01U;
  CHECK(track::identity::decode_record(bad_crc.data(), bad_crc.size(), sentinel) ==
        RecordDecodeStatus::kInvalid);
  CHECK(sentinel.generation == before.generation &&
        same_uuid(sentinel.device_id, before.device_id));
  CHECK(track::identity::decode_record(expected, sizeof(expected) - 1U, sentinel) ==
        RecordDecodeStatus::kInvalid);
  CHECK(sentinel.generation == before.generation);

  uint8_t untouched[kRecordSize];
  std::memset(untouched, 0x5A, sizeof(untouched));
  Record nil = make_record(0U, Uuid());
  CHECK(!track::identity::encode_record(nil, untouched, sizeof(untouched)));
  for (size_t i = 0; i < sizeof(untouched); ++i) CHECK(untouched[i] == 0x5AU);
  CHECK(!track::identity::encode_record(record, untouched, sizeof(untouched) - 1U));
  for (size_t i = 0; i < sizeof(untouched); ++i) CHECK(untouched[i] == 0x5AU);
  Record mismatch = make_record(3U, record.device_id);
  mismatch.last_boot = 2U;
  CHECK(!track::identity::encode_record(mismatch, untouched, sizeof(untouched)));
  CHECK(!track::identity::uuid_is_nil(record.device_id));
  CHECK(track::identity::uuid_is_nil(nil.device_id));
}

void test_sequence_counter_edges() {
  SequenceCounter counter;
  uint32_t first = 0xDEADBEEFU;
  CHECK(counter.reserve(kMaxPointCountPerChunk, first) && first == 0U);
  CHECK(counter.next() == kMaxPointCountPerChunk);
  CHECK(counter.reserve(kMaxPointCountPerChunk, first) &&
        first == kMaxPointCountPerChunk);
  CHECK(counter.next() == 2U * kMaxPointCountPerChunk);
  const uint64_t unchanged = counter.next();
  first = 0xDEADBEEFU;
  CHECK(!counter.reserve(0U, first));
  CHECK(first == 0xDEADBEEFU && counter.next() == unchanged);

  SequenceCounter edge(std::numeric_limits<uint32_t>::max() - 2U);
  first = 0U;
  CHECK(edge.reserve(2U, first));
  CHECK(first == std::numeric_limits<uint32_t>::max() - 2U);
  CHECK(edge.next() == std::numeric_limits<uint32_t>::max());
  CHECK(edge.reserve(1U, first));
  CHECK(first == std::numeric_limits<uint32_t>::max());
  CHECK(edge.next() == static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) +
                           1U);
  first = 0x12345678U;
  CHECK(!edge.reserve(1U, first));
  CHECK(first == 0x12345678U);
  CHECK(edge.next() == static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) +
                           1U);
  CHECK(!edge.reserve(2U, first));
}

void test_explicit_provisioning_and_boot() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  boot.boot_sequence = 0xA5A5A5A5U;
  boot.device_id = make_uuid(0xA0U);
  const BootSession boot_sentinel = boot;
  CHECK(store.mount() == Status::kEmpty);
  CHECK(store.status() == Status::kEmpty && store.mounted());
  CHECK(backend.writes == 0U);
  CHECK(store.alloc_boot_sequence(boot) == Status::kNotProvisioned);
  CHECK(boot.boot_sequence == boot_sentinel.boot_sequence &&
        same_uuid(boot.device_id, boot_sentinel.device_id));
  ChunkReservation no_session = {};
  no_session.boot_sequence = 123U;
  CHECK(store.reserve_chunk(1U, no_session) == Status::kNotProvisioned);
  CHECK(no_session.boot_sequence == 123U);
  CHECK(backend.writes == 0U);

  Uuid nil = {};
  CHECK(store.provision(nil) == Status::kInvalidArgument);
  CHECK(backend.writes == 0U && store.status() == Status::kEmpty);
  const Uuid id = make_uuid(0x20U);
  CHECK(store.provision(id) == Status::kOk);
  CHECK(store.provisioned() && !store.session_active());
  CHECK(backend.writes == 2U);
  CHECK(store.provision(id) == Status::kAlreadyProvisioned);
  CHECK(backend.writes == 2U);

  const unsigned reads_before_boot = backend.reads;
  CHECK(store.alloc_boot_sequence(boot) == Status::kOk);
  CHECK(boot.boot_sequence == 1U && same_uuid(boot.device_id, id));
  CHECK(boot.boot_sequence != 0U && store.current_boot_sequence() == 1U);
  CHECK(backend.writes == 3U && backend.reads > reads_before_boot);
  CHECK(backend.last_write_readback_exact);
  CHECK(backend.events.size() >= 2U);
  size_t write_index = backend.events.size();
  size_t verified_read_index = backend.events.size();
  for (size_t i = 0; i < backend.events.size(); ++i) {
    if (backend.events[i][0] == 'w') write_index = i;
    if (write_index < i && backend.events[i][0] == 'r') verified_read_index = i;
  }
  CHECK(write_index < verified_read_index);

  DeviceIdentityStore another(backend);
  CHECK(another.mount() == Status::kOk);
  BootSession next = {};
  CHECK(another.alloc_boot_sequence(next) == Status::kOk);
  CHECK(next.boot_sequence == 2U && same_uuid(next.device_id, id));
  CHECK(boot_sentinel.boot_sequence == 0xA5A5A5A5U);
}

void test_pair_fault_matrix() {
  const Uuid id = make_uuid(0x10U);
  const Record genesis = make_record(0U, id);
  for (uint8_t bad_slot = 0; bad_slot < 2U; ++bad_slot) {
    for (unsigned case_id = 0; case_id < 6U; ++case_id) {
      MemoryBackend backend;
      put_record(backend, static_cast<uint8_t>(1U - bad_slot), genesis);
      if (case_id == 0U) {
        backend.banks[bad_slot].present = false;
      } else if (case_id == 1U) {
        backend.banks[bad_slot].present = true;
        backend.banks[bad_slot].bytes.assign(kRecordSize - 1U, 0x44U);
      } else if (case_id == 2U) {
        backend.banks[bad_slot].present = true;
        backend.banks[bad_slot].bytes = encode(genesis);
        backend.banks[bad_slot].bytes[39] ^= 0x80U;
      } else if (case_id == 3U) {
        backend.banks[bad_slot].present = true;
        backend.banks[bad_slot].bytes = encode(genesis);
        backend.banks[bad_slot].bytes[4] = 2U;
        set_crc(backend.banks[bad_slot].bytes);
      } else if (case_id == 4U) {
        backend.banks[bad_slot].present = true;
        backend.banks[bad_slot].bytes = encode(genesis);
        backend.banks[bad_slot].bytes.push_back(0U);
      } else {
        backend.banks[bad_slot].present = true;
        backend.banks[bad_slot].bytes = encode(genesis);
        backend.error_slot = bad_slot;
      }
      DeviceIdentityStore store(backend);
      const Status mounted = store.mount();
      CHECK(mounted != Status::kOk && mounted != Status::kEmpty);
      CHECK(backend.writes == 1U); // The only write built the fixture.
      CHECK(!store.provisioned());
    }
  }
}

void test_pair_consistency() {
  const Uuid first_id = make_uuid(0x20U);
  const Uuid second_id = make_uuid(0x80U);
  MemoryBackend backend;
  put_record(backend, 0U, make_record(3U, first_id));
  put_record(backend, 1U, make_record(4U, first_id));
  const unsigned writes = backend.writes;
  DeviceIdentityStore store(backend);
  CHECK(store.mount() == Status::kOk);
  CHECK(store.current_boot_sequence() == 4U && store.provisioned());
  CHECK(backend.writes == writes);
  BootSession boot = {};
  CHECK(store.alloc_boot_sequence(boot) == Status::kOk);
  CHECK(boot.boot_sequence == 5U);

  MemoryBackend gap;
  put_record(gap, 0U, make_record(1U, first_id));
  put_record(gap, 1U, make_record(3U, first_id));
  DeviceIdentityStore gap_store(gap);
  CHECK(gap_store.mount() == Status::kGenerationMismatch);

  MemoryBackend reversed_gap;
  put_record(reversed_gap, 0U, make_record(3U, first_id));
  put_record(reversed_gap, 1U, make_record(1U, first_id));
  DeviceIdentityStore reversed_store(reversed_gap);
  CHECK(reversed_store.mount() == Status::kGenerationMismatch);

  MemoryBackend uuid_mismatch;
  put_record(uuid_mismatch, 0U, make_record(7U, first_id));
  put_record(uuid_mismatch, 1U, make_record(8U, second_id));
  DeviceIdentityStore uuid_store(uuid_mismatch);
  CHECK(uuid_store.mount() == Status::kUuidMismatch);

  MemoryBackend equal_conflict;
  put_record(equal_conflict, 0U, make_record(5U, first_id));
  put_record(equal_conflict, 1U, make_record(5U, second_id));
  DeviceIdentityStore conflict_store(equal_conflict);
  CHECK(conflict_store.mount() == Status::kAmbiguous ||
        conflict_store.status() == Status::kUuidMismatch);

  MemoryBackend equal_nonzero;
  put_record(equal_nonzero, 0U, make_record(5U, first_id));
  put_record(equal_nonzero, 1U, make_record(5U, first_id));
  DeviceIdentityStore equal_store(equal_nonzero);
  CHECK(equal_store.mount() == Status::kAmbiguous);

  MemoryBackend unequal_fields;
  std::vector<uint8_t> inconsistent = encode(make_record(3U, first_id));
  store_le32(inconsistent.data() + 12U, 2U);
  set_crc(inconsistent);
  unequal_fields.banks[0].present = true;
  unequal_fields.banks[0].bytes = inconsistent;
  unequal_fields.banks[1].present = true;
  unequal_fields.banks[1].bytes = encode(make_record(4U, first_id));
  DeviceIdentityStore unequal_store(unequal_fields);
  CHECK(unequal_store.mount() == Status::kCorrupt);
  CHECK(writes == 2U && backend.writes == writes + 1U);
  CHECK(unequal_fields.writes == 0U);

  MemoryBackend exhausted;
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  put_record(exhausted, 0U, make_record(maximum, first_id));
  put_record(exhausted, 1U, make_record(maximum - 1U, first_id));
  DeviceIdentityStore exhausted_store(exhausted);
  CHECK(exhausted_store.mount() == Status::kOk);
  const unsigned exhausted_writes = exhausted.writes;
  BootSession unchanged = {};
  unchanged.boot_sequence = 0x1234U;
  CHECK(exhausted_store.alloc_boot_sequence(unchanged) ==
        Status::kSequenceExhausted);
  CHECK(unchanged.boot_sequence == 0x1234U && !exhausted_store.session_active());
  CHECK(exhausted.writes == exhausted_writes);
}

void test_live_counters_and_session_rules() {
  const Uuid id = make_uuid(0x31U);
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  CHECK(store.mount() == Status::kEmpty);
  CHECK(store.provision(id) == Status::kOk);
  ChunkReservation before_boot = {};
  before_boot.chunk_sequence = 0xABCDU;
  CHECK(store.reserve_chunk(1U, before_boot) == Status::kNoActiveSession);
  CHECK(before_boot.chunk_sequence == 0xABCDU);

  BootSession first = {};
  first.boot_sequence = 0xFFFFFFFFU;
  first.device_id = make_uuid(0xE0U);
  CHECK(store.alloc_boot_sequence(first) == Status::kOk);
  CHECK(first.boot_sequence == 1U && same_uuid(first.device_id, id));
  ChunkReservation chunk = {};
  CHECK(store.reserve_chunk(96U, chunk) == Status::kOk);
  CHECK(chunk.boot_sequence == 1U && chunk.chunk_sequence == 0U);
  CHECK(chunk.first_point_sequence == 0U && chunk.point_count == 96U);
  CHECK(same_uuid(chunk.device_id, id));

  const unsigned writes = backend.writes;
  CHECK(store.mount() == Status::kAlreadyActive);
  CHECK(store.load() == Status::kAlreadyActive);
  BootSession rejected = {};
  rejected.boot_sequence = 0xABCDEF01U;
  rejected.device_id = make_uuid(0xA0U);
  CHECK(store.alloc_boot_sequence(rejected) == Status::kAlreadyActive);
  CHECK(rejected.boot_sequence == 0xABCDEF01U);
  CHECK(same_uuid(rejected.device_id, make_uuid(0xA0U)));
  CHECK(store.session_active() && store.current_boot_sequence() == 1U);
  CHECK(backend.writes == writes);

  ChunkReservation after_remount = {};
  CHECK(store.reserve_chunk(1U, after_remount) == Status::kOk);
  CHECK(after_remount.chunk_sequence == 1U);
  CHECK(after_remount.first_point_sequence == 96U);

  ChunkReservation sentinel = {};
  sentinel.boot_sequence = 77U;
  sentinel.chunk_sequence = 78U;
  sentinel.first_point_sequence = 79U;
  sentinel.point_count = 80U;
  CHECK(store.reserve_chunk(0U, sentinel) == Status::kInvalidArgument);
  CHECK(sentinel.boot_sequence == 77U && sentinel.chunk_sequence == 78U &&
        sentinel.first_point_sequence == 79U && sentinel.point_count == 80U);
  CHECK(store.reserve_chunk(kMaxPointCountPerChunk + 1U, sentinel) ==
        Status::kInvalidArgument);
  CHECK(sentinel.boot_sequence == 77U && sentinel.chunk_sequence == 78U &&
        sentinel.first_point_sequence == 79U && sentinel.point_count == 80U);
  CHECK(store.reserve_chunk(96U, sentinel) == Status::kOk);
  CHECK(sentinel.chunk_sequence == 2U && sentinel.first_point_sequence == 97U);

  DeviceIdentityStore fresh(backend);
  CHECK(fresh.mount() == Status::kOk);
  BootSession second = {};
  CHECK(fresh.alloc_boot_sequence(second) == Status::kOk);
  CHECK(second.boot_sequence == 2U && same_uuid(second.device_id, id));
  ChunkReservation reset_for_new_boot = {};
  CHECK(fresh.reserve_chunk(1U, reset_for_new_boot) == Status::kOk);
  CHECK(reset_for_new_boot.boot_sequence == 2U &&
        reset_for_new_boot.chunk_sequence == 0U &&
        reset_for_new_boot.first_point_sequence == 0U);
}

void assert_failed_allocation_latches(MemoryBackend &backend) {
  DeviceIdentityStore store(backend);
  CHECK(store.mount() == Status::kOk);
  const unsigned writes_before = backend.writes;
  BootSession output = {};
  output.boot_sequence = 0xFEEDBEEFU;
  output.device_id = make_uuid(0xD0U);
  const BootSession before = output;
  CHECK(store.alloc_boot_sequence(output) == Status::kFaulted);
  CHECK(store.status() == Status::kFaulted && !store.session_active());
  CHECK(output.boot_sequence == before.boot_sequence &&
        same_uuid(output.device_id, before.device_id));
  const unsigned writes_after = backend.writes;
  CHECK(writes_after == writes_before + 1U);
  CHECK(store.mount() == Status::kFaulted);
  CHECK(store.alloc_boot_sequence(output) == Status::kFaulted);
  CHECK(backend.writes == writes_after);
  ChunkReservation chunk = {};
  chunk.boot_sequence = 0xCAFEU;
  CHECK(store.reserve_chunk(1U, chunk) == Status::kFaulted);
  CHECK(chunk.boot_sequence == 0xCAFEU && backend.writes == writes_after);
}

void test_allocation_failure_cuts() {
  const Uuid id = make_uuid(0x41U);
  MemoryBackend baseline;
  seed_boot_one(baseline, id);

  for (size_t prefix = 0; prefix < kRecordSize; ++prefix) {
    MemoryBackend cut = baseline;
    cut.set_write_fault(WriteFault::kPrefixThenFail, prefix);
    assert_failed_allocation_latches(cut);
    DeviceIdentityStore recovered(cut);
    const Status mounted = recovered.mount();
    if (mounted == Status::kOk) {
      BootSession next = {};
      CHECK(recovered.alloc_boot_sequence(next) == Status::kOk);
      CHECK(next.boot_sequence > 1U);
    } else {
      CHECK(mounted != Status::kEmpty);
      BootSession untouched = {};
      untouched.boot_sequence = 0xFACEU;
      CHECK(recovered.alloc_boot_sequence(untouched) != Status::kOk);
      CHECK(untouched.boot_sequence == 0xFACEU);
    }
  }

  MemoryBackend before_write = baseline;
  before_write.set_write_fault(WriteFault::kFailBefore);
  assert_failed_allocation_latches(before_write);
  DeviceIdentityStore before_recovery(before_write);
  CHECK(before_recovery.mount() == Status::kOk);
  BootSession after_before = {};
  CHECK(before_recovery.alloc_boot_sequence(after_before) == Status::kOk);
  CHECK(after_before.boot_sequence == 2U);

  MemoryBackend full_committed_lost_ack = baseline;
  full_committed_lost_ack.set_write_fault(WriteFault::kFullThenFail);
  assert_failed_allocation_latches(full_committed_lost_ack);
  DeviceIdentityStore full_recovery(full_committed_lost_ack);
  CHECK(full_recovery.mount() == Status::kOk);
  BootSession after_full = {};
  CHECK(full_recovery.alloc_boot_sequence(after_full) == Status::kOk);
  CHECK(after_full.boot_sequence == 3U);

  for (int failed_read = 1; failed_read <= 3; ++failed_read) {
    MemoryBackend readback_failure = baseline;
    readback_failure.fail_read_after_write = failed_read;
    assert_failed_allocation_latches(readback_failure);
    DeviceIdentityStore read_recovery(readback_failure);
    const Status mounted = read_recovery.mount();
    if (mounted == Status::kOk) {
      readback_failure.fail_read_after_write = -1;
      BootSession next = {};
      CHECK(read_recovery.alloc_boot_sequence(next) == Status::kOk);
      CHECK(next.boot_sequence == 3U);
    } else {
      CHECK(mounted != Status::kEmpty);
    }
  }

  MemoryBackend short_readback = baseline;
  short_readback.short_read_after_write = 1;
  assert_failed_allocation_latches(short_readback);
  DeviceIdentityStore short_recovery(short_readback);
  const Status short_mounted = short_recovery.mount();
  if (short_mounted == Status::kOk) {
    short_readback.short_read_after_write = -1;
    BootSession next = {};
    CHECK(short_recovery.alloc_boot_sequence(next) == Status::kOk);
    CHECK(next.boot_sequence == 3U);
  } else {
    CHECK(short_mounted != Status::kEmpty);
  }
}

void test_provisioning_failure_cuts() {
  struct ProvisionCut {
    uint8_t slot;
    WriteFault fault;
    Status fresh_status;
    bool bank_a_present;
    bool bank_b_present;
  };
  const ProvisionCut cuts[] = {
      {0U, WriteFault::kFailBefore, Status::kEmpty, false, false},
      {0U, WriteFault::kFullThenFail, Status::kCorrupt, true, false},
      {1U, WriteFault::kFailBefore, Status::kCorrupt, true, false},
      {1U, WriteFault::kFullThenFail, Status::kOk, true, true},
  };
  const Uuid id = make_uuid(0x71U);
  for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
    MemoryBackend backend;
    DeviceIdentityStore store(backend);
    CHECK(store.mount() == Status::kEmpty);
    backend.set_write_fault(cuts[i].fault, 0U, cuts[i].slot);
    CHECK(store.provision(id) == Status::kFaulted);
    CHECK(store.status() == Status::kFaulted && !store.provisioned());
    const unsigned writes_after_failure = backend.writes;
    CHECK(writes_after_failure == (cuts[i].slot == 0U ? 1U : 2U));
    CHECK(store.mount() == Status::kFaulted);
    CHECK(store.provision(id) == Status::kFaulted);
    BootSession blocked = {};
    blocked.boot_sequence = 0xA55AU;
    CHECK(store.alloc_boot_sequence(blocked) == Status::kFaulted);
    CHECK(blocked.boot_sequence == 0xA55AU);
    ChunkReservation blocked_chunk = {};
    blocked_chunk.chunk_sequence = 0xBEEFU;
    CHECK(store.reserve_chunk(1U, blocked_chunk) == Status::kFaulted);
    CHECK(blocked_chunk.chunk_sequence == 0xBEEFU);
    CHECK(backend.writes == writes_after_failure);

    DeviceIdentityStore recovered(backend);
    CHECK(recovered.mount() == cuts[i].fresh_status);
    CHECK(backend.banks[0].present == cuts[i].bank_a_present);
    CHECK(backend.banks[1].present == cuts[i].bank_b_present);
    CHECK(backend.writes == writes_after_failure);
    if (cuts[i].fresh_status == Status::kOk) {
      BootSession boot = {};
      CHECK(recovered.alloc_boot_sequence(boot) == Status::kOk);
      CHECK(boot.boot_sequence == 1U && same_uuid(boot.device_id, id));
    } else {
      BootSession withheld = {};
      withheld.boot_sequence = 0xCAFEU;
      CHECK(recovered.alloc_boot_sequence(withheld) != Status::kOk);
      CHECK(withheld.boot_sequence == 0xCAFEU);
      CHECK(backend.writes == writes_after_failure);
    }
  }
}

void test_empty_and_nonnil_guards() {
  const Uuid id = make_uuid(0x50U);
  MemoryBackend one_bank;
  put_record(one_bank, 0U, make_record(0U, id));
  DeviceIdentityStore cannot_reprovision(one_bank);
  CHECK(cannot_reprovision.mount() != Status::kEmpty);
  const unsigned writes = one_bank.writes;
  CHECK(cannot_reprovision.provision(make_uuid(0x70U)) != Status::kOk);
  CHECK(one_bank.writes == writes);

  MemoryBackend nil_record;
  std::vector<uint8_t> raw = encode(make_record(0U, id));
  std::memset(raw.data() + 16U, 0, 16U);
  set_crc(raw);
  nil_record.banks[0].present = true;
  nil_record.banks[0].bytes = raw;
  nil_record.banks[1].present = true;
  nil_record.banks[1].bytes = raw;
  DeviceIdentityStore nil_store(nil_record);
  CHECK(nil_store.mount() == Status::kCorrupt);
  CHECK(nil_record.writes == 0U);

  MemoryBackend empty;
  DeviceIdentityStore unmounted(empty);
  CHECK(unmounted.provision(id) == Status::kNotMounted);
  CHECK(empty.writes == 0U);
}

void test_corrupt_latest_never_falls_back() {
  const Uuid id = make_uuid(0x60U);
  for (uint8_t latest_slot = 0; latest_slot < 2U; ++latest_slot) {
    MemoryBackend backend;
    put_record(backend, static_cast<uint8_t>(1U - latest_slot),
               make_record(0U, id));
    backend.banks[latest_slot].present = true;
    backend.banks[latest_slot].bytes = encode(make_record(1U, id));
    backend.banks[latest_slot].bytes[36] ^= 0x01U;
    const unsigned fixture_writes = backend.writes;

    DeviceIdentityStore store(backend);
    CHECK(store.mount() == Status::kCorrupt);
    CHECK(!store.provisioned() && !store.session_active());
    const unsigned before_attempts = backend.writes;
    CHECK(store.provision(id) == Status::kCorrupt);
    BootSession output = {};
    output.boot_sequence = 0xF00DU;
    CHECK(store.alloc_boot_sequence(output) == Status::kCorrupt);
    CHECK(output.boot_sequence == 0xF00DU);
    CHECK(backend.writes == before_attempts &&
          before_attempts == fixture_writes);
  }

  MemoryBackend stale_empty;
  DeviceIdentityStore stale_store(stale_empty);
  CHECK(stale_store.mount() == Status::kEmpty);
  const Uuid id_b = make_uuid(0x90U);
  put_record(stale_empty, 0U, make_record(0U, id_b));
  put_record(stale_empty, 1U, make_record(0U, id_b));
  const unsigned writes_after_external_provision = stale_empty.writes;
  CHECK(stale_store.provision(id_b) == Status::kFaulted);
  CHECK(stale_store.status() == Status::kFaulted);
  CHECK(stale_empty.writes == writes_after_external_provision);
}

}  // namespace

int main() {
  test_record_codec_and_golden();
  test_sequence_counter_edges();
  test_explicit_provisioning_and_boot();
  test_pair_fault_matrix();
  test_pair_consistency();
  test_live_counters_and_session_rules();
  test_allocation_failure_cuts();
  test_provisioning_failure_cuts();
  test_empty_and_nonnil_guards();
  test_corrupt_latest_never_falls_back();
  std::cout << "PASS device_identity_native checks=" << check_count << '\n';
  return 0;
}
