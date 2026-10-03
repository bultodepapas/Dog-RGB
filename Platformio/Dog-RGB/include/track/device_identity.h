#pragma once

#include <stddef.h>
#include <stdint.h>

namespace track {
namespace identity {

static const uint8_t kBankA = 0U;
static const uint8_t kBankB = 1U;
static const size_t kRecordSize = 40U;
static const uint32_t kMaxPointCountPerChunk = 96U;

struct Uuid {
  // RFC 4122/network byte order, matching track::v3::ChunkV3::device_id.
  uint8_t bytes[16];
};

typedef Uuid DeviceId;

struct Record {
  uint32_t generation;
  uint32_t last_boot;
  Uuid device_id;
};

enum class RecordDecodeStatus : uint8_t {
  kValid = 0,
  kInvalid,
  kFutureVersion,
};

// Encodes/decodes the canonical 40-byte D3ID record. Encoding requires at
// least kRecordSize bytes; decoding requires exactly kRecordSize bytes. Input
// and output storage must be disjoint; the output is unchanged on failure.
// Generation must equal last_boot and a nil UUID is invalid.
bool encode_record(const Record &record, uint8_t *out, size_t out_capacity);
RecordDecodeStatus decode_record(const uint8_t *data, size_t data_size,
                                 Record &out);
bool uuid_is_nil(const Uuid &uuid);

enum class ReadStatus : uint8_t {
  kOk = 0,
  kMissing,
  kTooLarge,
  kError,
};

// Storage adapter seam. Slot 0 is bank A; slot 1 is bank B. `actual_size` is
// meaningful for kOk and kTooLarge. Implementations must not write beyond
// capacity; callers also check for kOk with actual_size > capacity. Writes may
// be partial or ambiguous when they return false; callers must
// treat any attempted mutation failure as uncertain and stop using the store.
class Backend {
 public:
  virtual ~Backend() {}
  virtual ReadStatus read(uint8_t slot, uint8_t *out, size_t capacity,
                          size_t &actual_size) = 0;
  virtual bool write(uint8_t slot, const uint8_t *data, size_t size) = 0;
};

enum class Status : uint8_t {
  kOk = 0,
  kEmpty,
  kInvalidArgument,
  kNotMounted,
  kNotProvisioned,
  kAlreadyProvisioned,
  kNoActiveSession,
  kStorageError,
  kOversized,
  kCorrupt,
  kFutureVersion,
  kAmbiguous,
  kUuidMismatch,
  kGenerationMismatch,
  kSequenceExhausted,
  kAlreadyActive,
  kFaulted,
};

struct BootSession {
  Uuid device_id;
  uint32_t boot_sequence;
};

struct ChunkReservation {
  Uuid device_id;
  uint32_t boot_sequence;
  uint32_t chunk_sequence;
  uint32_t first_point_sequence;
  uint32_t point_count;
};

// Pure, non-wrapping uint32 range allocator. A successful reservation may
// consume UINT32_MAX; next() then reports UINT32_MAX + 1 and later requests
// fail. Failed calls leave both the counter and `first` unchanged.
class SequenceCounter {
 public:
  explicit SequenceCounter(uint32_t first = 0U);

  bool reserve(uint32_t count, uint32_t &first);
  uint64_t next() const;

 private:
  uint64_t next_;
};

// Own one instance per backend and serialize all calls that touch it. The
// store intentionally cannot be copied or moved: copying an active store
// would duplicate volatile sequence counters for the same granted boot.
// Construction is side-effect free; mount() never formats or writes storage.
class DeviceIdentityStore {
 public:
  explicit DeviceIdentityStore(Backend &backend);
  DeviceIdentityStore(const DeviceIdentityStore &) = delete;
  DeviceIdentityStore &operator=(const DeviceIdentityStore &) = delete;
  DeviceIdentityStore(DeviceIdentityStore &&) = delete;
  DeviceIdentityStore &operator=(DeviceIdentityStore &&) = delete;

  Status mount();
  Status load();  // Alias for mount().

  // Explicit provisioning is accepted only after mount() returned kEmpty.
  // The caller supplies the UUID; this layer never generates one.
  Status provision(const Uuid &device_id);

  // Allocates and durably verifies the next nonzero boot sequence. A store
  // instance may grant at most one boot session.
  Status alloc_boot_sequence(BootSession &out);

  // Reserves one chunk and a contiguous point range (1..96 points). Chunk
  // and point numbering starts at zero for each granted boot session.
  Status reserve_chunk(uint32_t point_count, ChunkReservation &out);

  Status status() const;
  bool mounted() const;
  bool provisioned() const;
  bool session_active() const;
  // Persisted high-water mark; use a successful alloc_boot_sequence result
  // from this instance as the granted session identity.
  uint32_t current_boot_sequence() const;

 private:
  struct BankView {
    ReadStatus read_status;
    RecordDecodeStatus decode_status;
    size_t actual_size;
    Record record;
    uint8_t bytes[kRecordSize];
  };

  struct PairView {
    BankView banks[2];
    Status status;
    int8_t current_bank;
    uint32_t current_generation;
    Uuid device_id;
  };

  BankView read_bank(uint8_t slot);
  PairView read_pair();
  Status classify_pair(PairView &pair) const;
  bool write_record_verified(uint8_t slot, const Record &record,
                             const uint8_t *expected_bytes);
  bool pair_matches(const PairView &pair, uint8_t bank_a,
                    uint32_t generation_a, uint8_t bank_b,
                    uint32_t generation_b, const Uuid &device_id) const;
  void latch_fault();

  Backend &backend_;
  bool mounted_;
  bool provisioned_;
  bool session_active_;
  bool faulted_;
  Status status_;
  Uuid device_id_;
  uint32_t current_boot_sequence_;
  uint32_t bank_generations_[2];
  int8_t current_bank_;
  SequenceCounter chunk_counter_;
  SequenceCounter point_counter_;
};

}  // namespace identity
}  // namespace track
