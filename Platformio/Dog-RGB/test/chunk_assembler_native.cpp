#include "track/chunk_assembler.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using track::identity::Backend;
using track::identity::BootSession;
using track::identity::ChunkReservation;
using track::identity::DeviceIdentityStore;
using track::identity::ReadStatus;
typedef track::identity::Status IdentityStatus;
using track::identity::Uuid;
using track::v3::AssemblerStatus;
using track::v3::ChunkAssembler;
using track::v3::ChunkSink;
using track::v3::Sha256Fn;
using track::v3::TimeQuality;
using track::v3::TrackPointV3;

unsigned long check_count = 0;

void check(bool value, const char *expression, int line) {
  ++check_count;
  if (!value) {
    std::cerr << "line " << line << ": " << expression << '\n';
    std::exit(1);
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

static_assert(!std::is_copy_constructible<ChunkAssembler>::value,
              "ChunkAssembler must not be copy constructible");
static_assert(!std::is_copy_assignable<ChunkAssembler>::value,
              "ChunkAssembler must not be copy assignable");
static_assert(!std::is_move_constructible<ChunkAssembler>::value,
              "ChunkAssembler must not be move constructible");
static_assert(!std::is_move_assignable<ChunkAssembler>::value,
              "ChunkAssembler must not be move assignable");
static_assert(track::v3::kMaxEncodedChunkSize == 1628U,
              "Track v3 frame bound changed; update the native contract test");

struct Bank {
  bool present;
  std::vector<uint8_t> bytes;
  Bank() : present(false), bytes() {}
};

class MemoryBackend : public Backend {
 public:
  Bank banks[2];
  unsigned reads;
  unsigned writes;

  MemoryBackend() : banks(), reads(0U), writes(0U) {}

  ReadStatus read(uint8_t slot, uint8_t *out, size_t capacity,
                  size_t &actual_size) override {
    ++reads;
    actual_size = 0U;
    if (slot >= 2U || out == nullptr) return ReadStatus::kError;
    const Bank &bank = banks[slot];
    if (!bank.present) return ReadStatus::kMissing;
    actual_size = bank.bytes.size();
    if (actual_size > capacity) return ReadStatus::kTooLarge;
    if (actual_size != 0U) std::memcpy(out, bank.bytes.data(), actual_size);
    return ReadStatus::kOk;
  }

  bool write(uint8_t slot, const uint8_t *data, size_t size) override {
    ++writes;
    if (slot >= 2U || data == nullptr) return false;
    banks[slot].present = true;
    banks[slot].bytes.assign(data, data + size);
    return true;
  }
};

struct HashContext {
  unsigned calls;
  unsigned failures;
  bool fail_next;
  std::vector<uint8_t> last_input;
  ChunkAssembler *reenter;
  TrackPointV3 reentry_point;
  AssemblerStatus reentry_status;
  bool have_reentry_status;

  HashContext()
      : calls(0U), failures(0U), fail_next(false), last_input(),
        reenter(nullptr), reentry_point(),
        reentry_status(AssemblerStatus::kOk), have_reentry_status(false) {}
};

int hex_nibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

std::string encode_hex(const uint8_t *data, size_t size) {
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(size * 2U);
  for (size_t i = 0U; i < size; ++i) {
    result[2U * i] = digits[(data[i] >> 4U) & 0x0FU];
    result[2U * i + 1U] = digits[data[i] & 0x0FU];
  }
  return result;
}

bool decode_hex(const std::string &text, uint8_t *out, size_t capacity,
                size_t &actual_size) {
  if (text.size() % 2U != 0U || text.size() / 2U > capacity) return false;
  actual_size = text.size() / 2U;
  for (size_t i = 0U; i < actual_size; ++i) {
    const int high = hex_nibble(text[2U * i]);
    const int low = hex_nibble(text[2U * i + 1U]);
    if (high < 0 || low < 0) return false;
    out[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool python_sha256(const uint8_t *data, size_t size, uint8_t digest[32],
                   void *opaque) {
  if (data == nullptr || digest == nullptr || opaque == nullptr) return false;
  HashContext *context = static_cast<HashContext *>(opaque);
  ++context->calls;
  context->last_input.assign(data, data + size);
  if (context->reenter != nullptr) {
    context->reentry_status =
        context->reenter->append(context->reentry_point,
                                 TimeQuality::kGnssTrusted);
    context->have_reentry_status = true;
    context->reenter = nullptr;
  }

  const bool fail = context->fail_next;
  context->fail_next = false;
  if (fail) ++context->failures;
  std::cout << (fail ? "SHA_FAIL " : "SHA ") << encode_hex(data, size)
            << '\n';
  std::cout.flush();

  std::string response;
  if (!std::getline(std::cin, response)) return false;
  if (fail) {
    CHECK(response == "HASHFAIL");
    return false;
  }
  const std::string prefix("HASH ");
  if (response.compare(0U, prefix.size(), prefix) != 0) return false;
  uint8_t parsed[32] = {};
  size_t parsed_size = 0U;
  if (!decode_hex(response.substr(prefix.size()), parsed, sizeof(parsed),
                  parsed_size) ||
      parsed_size != sizeof(parsed)) {
    return false;
  }
  std::memcpy(digest, parsed, sizeof(parsed));
  return true;
}

class MemorySink : public ChunkSink {
 public:
  bool allow;
  bool commit_then_reject;
  bool probe_reentry;
  ChunkAssembler *reenter;
  TrackPointV3 reentry_point;
  AssemblerStatus reentry_status;
  bool have_reentry_status;
  unsigned calls;
  unsigned logical_commits;
  std::vector<uint8_t> durable_frame;
  std::vector<std::vector<uint8_t> > attempts;

  MemorySink()
      : allow(true), commit_then_reject(false), probe_reentry(false),
        reenter(nullptr), reentry_point(),
        reentry_status(AssemblerStatus::kOk), have_reentry_status(false),
        calls(0U), logical_commits(0U), durable_frame(), attempts() {}

  bool accept(const uint8_t *bytes, size_t size) override {
    ++calls;
    const std::vector<uint8_t> candidate(bytes, bytes + size);
    attempts.push_back(candidate);
    if (probe_reentry && reenter != nullptr) {
      reentry_status = reenter->append(reentry_point,
                                       TimeQuality::kGnssTrusted);
      have_reentry_status = true;
      probe_reentry = false;
    }

    if (commit_then_reject) {
      if (durable_frame.empty()) {
        durable_frame = candidate;
        ++logical_commits;
      }
      commit_then_reject = false;
      return false;
    }
    if (!allow) return false;
    if (durable_frame.empty()) {
      durable_frame = candidate;
      ++logical_commits;
      return true;
    }
    // Model idempotent local commit when the first commit succeeded but its
    // response was lost: exact retries confirm the one durable logical frame.
    return durable_frame == candidate;
  }
};

Uuid make_uuid(const uint8_t bytes[16]) {
  Uuid uuid = {};
  std::memcpy(uuid.bytes, bytes, sizeof(uuid.bytes));
  return uuid;
}

const uint8_t kDeviceBytes[16] = {
    0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x46U, 0x77U,
    0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU};
const uint8_t kSecondDeviceBytes[16] = {
    0xFEU, 0xDCU, 0xBAU, 0x98U, 0x76U, 0x54U, 0x43U, 0x21U,
    0x8AU, 0xBCU, 0xDEU, 0xF0U, 0x12U, 0x34U, 0x56U, 0x78U};
const uint8_t kThirdDeviceBytes[16] = {
    0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0x4CU, 0xDEU,
    0x80U, 0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU};

TrackPointV3 fix_point(int32_t latitude, int32_t longitude, uint32_t utc,
                       uint16_t speed, uint8_t satellites, uint8_t flags) {
  TrackPointV3 point = {};
  point.lat_e7 = latitude;
  point.lon_e7 = longitude;
  point.utc_s = utc;
  point.speed_cmps = speed;
  point.satellites = satellites;
  point.flags = flags;
  return point;
}

TrackPointV3 moving_point(uint32_t utc) {
  return fix_point(47110111, -740720123, utc, 1234U, 10U,
                   static_cast<uint8_t>(track::v3::kFixValid |
                                        track::v3::kTimeTrusted |
                                        track::v3::kMovementEvidence));
}

TrackPointV3 stationary_point(uint32_t utc) {
  return fix_point(47110112, -740720124, utc, 0U, 7U,
                   static_cast<uint8_t>(track::v3::kFixValid |
                                        track::v3::kTimeTrusted |
                                        track::v3::kStationaryHeartbeat));
}

TrackPointV3 gap_point() {
  return fix_point(0, 0, 0U, track::v3::kSpeedUnavailable, 0U,
                   track::v3::kGap);
}

std::vector<uint8_t> payload_of(const TrackPointV3 *points, size_t count) {
  std::vector<uint8_t> result(count * track::v3::kPointSize);
  for (size_t i = 0U; i < count; ++i) {
    CHECK(track::v3::encode_point(points[i],
                                  result.data() + i * track::v3::kPointSize,
                                  track::v3::kPointSize) ==
          track::v3::Status::kOk);
  }
  return result;
}

bool same_reservation(const ChunkReservation &left,
                      const ChunkReservation &right) {
  return std::memcmp(left.device_id.bytes, right.device_id.bytes,
                     sizeof(left.device_id.bytes)) == 0 &&
         left.boot_sequence == right.boot_sequence &&
         left.chunk_sequence == right.chunk_sequence &&
         left.first_point_sequence == right.first_point_sequence &&
         left.point_count == right.point_count;
}

ChunkReservation get_reservation(const ChunkAssembler &assembler) {
  ChunkReservation result = {};
  CHECK(assembler.reservation(result));
  return result;
}

void prepare_store(MemoryBackend &backend, DeviceIdentityStore &store,
                   const uint8_t device_bytes[16], BootSession &boot) {
  CHECK(backend.writes == 0U);
  CHECK(store.mount() == IdentityStatus::kEmpty);
  CHECK(store.provision(make_uuid(device_bytes)) == IdentityStatus::kOk);
  CHECK(store.alloc_boot_sequence(boot) == IdentityStatus::kOk);
  CHECK(boot.boot_sequence == 1U);
  CHECK(std::memcmp(boot.device_id.bytes, device_bytes,
                    sizeof(boot.device_id.bytes)) == 0);
}

void emit_frame(const char *label, const std::vector<uint8_t> &bytes) {
  CHECK(!bytes.empty());
  std::cout << "FRAME " << label << ' ' << encode_hex(bytes.data(), bytes.size())
            << '\n';
  std::cout.flush();
}

void test_pregrant_gating_and_no_storage_mutation() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  CHECK(store.mount() == IdentityStatus::kEmpty);
  const unsigned reads_after_mount = backend.reads;
  const unsigned writes_after_mount = backend.writes;

  HashContext hash;
  ChunkAssembler assembler(store, python_sha256, &hash);
  CHECK(backend.reads == reads_after_mount);
  CHECK(backend.writes == writes_after_mount);
  const TrackPointV3 valid = moving_point(1750000000U);
  CHECK(assembler.append(valid, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kNoActiveSession);
  CHECK(assembler.point_count() == 0U && !assembler.has_reservation() &&
        !assembler.has_frame());
  CHECK(assembler.seal(false) == AssemblerStatus::kEmpty);
  CHECK(backend.writes == 0U && hash.calls == 0U);

  CHECK(store.provision(make_uuid(kDeviceBytes)) == IdentityStatus::kOk);
  const unsigned writes_after_provision = backend.writes;
  CHECK(writes_after_provision == 2U);
  CHECK(assembler.append(valid, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kNoActiveSession);
  CHECK(assembler.point_count() == 0U && !assembler.has_reservation());
  CHECK(backend.writes == writes_after_provision && hash.calls == 0U);

  BootSession boot = {};
  CHECK(store.alloc_boot_sequence(boot) == IdentityStatus::kOk);
  const unsigned writes_after_grant = backend.writes;
  CHECK(assembler.append(valid, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  CHECK(backend.writes == writes_after_grant);
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  CHECK(backend.writes == writes_after_grant);
  CHECK(hash.calls == 1U);
}

void test_validation_retry_backpressure_reentry_and_next_sequence() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  prepare_store(backend, store, kDeviceBytes, boot);
  HashContext hash;
  ChunkAssembler assembler(store, python_sha256, &hash);
  CHECK(sizeof(assembler) < 6U * 1024U);

  const TrackPointV3 first = moving_point(1750000000U);
  const TrackPointV3 second = stationary_point(1750000000U);
  const TrackPointV3 third = moving_point(1750000001U);
  CHECK(assembler.append(first, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  CHECK(assembler.point_count() == 1U);
  CHECK(assembler.append(second, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  CHECK(assembler.point_count() == 2U);

  const unsigned writes = backend.writes;
  const TrackPointV3 no_gap = fix_point(0, 0, 0U,
                                        track::v3::kSpeedUnavailable, 0U, 0U);
  CHECK(assembler.append(no_gap, TimeQuality::kUnknown) ==
        AssemblerStatus::kInvalidPoint);
  CHECK(assembler.point_count() == 2U && !assembler.has_reservation() &&
        !assembler.has_frame());
  CHECK(assembler.append(first, TimeQuality::kLegacyMinute) ==
        AssemblerStatus::kInvalidTimeQuality);
  CHECK(assembler.point_count() == 2U);
  TrackPointV3 legacy_point = first;
  legacy_point.flags = static_cast<uint8_t>(legacy_point.flags |
                                             track::v3::kLegacyV2);
  CHECK(assembler.append(legacy_point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kInvalidPoint);
  CHECK(assembler.point_count() == 2U);
  CHECK(assembler.append(first, static_cast<TimeQuality>(255U)) ==
        AssemblerStatus::kInvalidTimeQuality);
  TrackPointV3 bad_coordinate = first;
  bad_coordinate.lat_e7 = 900000001;
  CHECK(assembler.append(bad_coordinate, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kInvalidPoint);
  TrackPointV3 missing_known_time = fix_point(
      47110111, -740720123, 0U, 1234U, 10U, track::v3::kFixValid);
  CHECK(assembler.append(missing_known_time, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kInvalidTimeQuality);
  TrackPointV3 unexpected_unknown_time = moving_point(1750000000U);
  CHECK(assembler.append(unexpected_unknown_time, TimeQuality::kUnknown) ==
        AssemblerStatus::kInvalidTimeQuality);
  CHECK(assembler.point_count() == 2U && backend.writes == writes);

  TrackPointV3 backwards = moving_point(1749999999U);
  CHECK(assembler.append(backwards, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kNeedsSeal);
  CHECK(assembler.point_count() == 2U);
  CHECK(assembler.append(first, TimeQuality::kApproximatePersisted) ==
        AssemblerStatus::kNeedsSeal);
  CHECK(assembler.point_count() == 2U);

  hash.reenter = &assembler;
  hash.reentry_point = third;
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  CHECK(hash.have_reentry_status &&
        hash.reentry_status == AssemblerStatus::kBusy);
  const TrackPointV3 expected_points[2] = {first, second};
  const std::vector<uint8_t> expected_payload =
      payload_of(expected_points, 2U);
  CHECK(hash.calls == 1U && hash.last_input == expected_payload);
  const ChunkReservation first_reservation = get_reservation(assembler);
  CHECK(first_reservation.boot_sequence == boot.boot_sequence &&
        first_reservation.chunk_sequence == 0U &&
        first_reservation.first_point_sequence == 0U &&
        first_reservation.point_count == 2U);
  CHECK(assembler.has_frame());
  CHECK(assembler.frame_size() == track::v3::kChunkHeaderSize +
                                      2U * track::v3::kPointSize);
  CHECK(assembler.frame_size() <= track::v3::kMaxEncodedChunkSize);
  CHECK(assembler.seal(true) == AssemblerStatus::kInvalidArgument);
  CHECK(assembler.has_frame() &&
        same_reservation(first_reservation, get_reservation(assembler)) &&
        hash.calls == 1U);

  MemorySink sink;
  sink.commit_then_reject = true;
  sink.reenter = &assembler;
  sink.reentry_point = third;
  sink.probe_reentry = true;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kSinkRejected);
  CHECK(sink.have_reentry_status &&
        sink.reentry_status == AssemblerStatus::kBusy);
  CHECK(sink.calls == 1U && sink.logical_commits == 1U &&
        !sink.durable_frame.empty());
  CHECK(assembler.has_frame() && assembler.has_reservation() &&
        assembler.point_count() == 2U);
  CHECK(assembler.append(third, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kPending);
  CHECK(assembler.point_count() == 2U && assembler.has_frame());
  const unsigned hash_calls_after_seal = hash.calls;
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  CHECK(hash.calls == hash_calls_after_seal);

  sink.probe_reentry = true;
  sink.reenter = &assembler;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kOk);
  CHECK(sink.have_reentry_status &&
        sink.reentry_status == AssemblerStatus::kBusy);
  CHECK(sink.calls == 2U && sink.attempts.size() == 2U &&
        sink.attempts[0] == sink.attempts[1] &&
        sink.durable_frame == sink.attempts[0] &&
        sink.logical_commits == 1U);
  CHECK(!assembler.has_frame() && !assembler.has_reservation() &&
        assembler.point_count() == 0U && assembler.frame_size() == 0U);
  emit_frame("golden", sink.durable_frame);

  CHECK(assembler.append(third, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  CHECK(assembler.point_count() == 1U);
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  const ChunkReservation second_reservation = get_reservation(assembler);
  CHECK(second_reservation.chunk_sequence == 1U &&
        second_reservation.first_point_sequence == 2U &&
        second_reservation.point_count == 1U &&
        second_reservation.boot_sequence == boot.boot_sequence);
  CHECK(assembler.frame_size() == track::v3::kChunkHeaderSize +
                                      track::v3::kPointSize);
  MemorySink next_sink;
  CHECK(assembler.handoff(next_sink) == AssemblerStatus::kOk);
  CHECK(next_sink.logical_commits == 1U &&
        next_sink.durable_frame == next_sink.attempts[0]);
  emit_frame("next", next_sink.durable_frame);
}

void test_failed_sha_keeps_one_reservation_and_same_frame_retry() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  prepare_store(backend, store, kSecondDeviceBytes, boot);
  HashContext hash;
  ChunkAssembler assembler(store, python_sha256, &hash);
  const TrackPointV3 point = moving_point(1750000200U);
  const TrackPointV3 next_point = stationary_point(1750000201U);
  CHECK(assembler.append(point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  hash.fail_next = true;
  hash.reenter = &assembler;
  hash.reentry_point = next_point;
  CHECK(assembler.seal(false) == AssemblerStatus::kEncodeFailure);
  CHECK(hash.calls == 1U && hash.failures == 1U &&
        hash.have_reentry_status &&
        hash.reentry_status == AssemblerStatus::kBusy);
  CHECK(assembler.point_count() == 1U && assembler.has_reservation() &&
        !assembler.has_frame());
  const ChunkReservation reserved = get_reservation(assembler);
  CHECK(reserved.chunk_sequence == 0U && reserved.first_point_sequence == 0U &&
        reserved.point_count == 1U && reserved.boot_sequence == boot.boot_sequence);
  CHECK(assembler.append(next_point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kPending);
  CHECK(assembler.point_count() == 1U && assembler.has_reservation() &&
        same_reservation(reserved, get_reservation(assembler)));

  CHECK(assembler.seal(true) == AssemblerStatus::kInvalidArgument);
  CHECK(hash.calls == 1U &&
        same_reservation(reserved, get_reservation(assembler)));
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  CHECK(hash.calls == 2U && hash.failures == 1U && assembler.has_frame() &&
        assembler.has_reservation() &&
        same_reservation(reserved, get_reservation(assembler)));
  CHECK(assembler.frame_size() == track::v3::kChunkHeaderSize +
                                      track::v3::kPointSize);
  CHECK(assembler.frame_size() <= track::v3::kMaxEncodedChunkSize);

  MemorySink sink;
  sink.commit_then_reject = true;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kSinkRejected);
  const std::vector<uint8_t> first_frame = sink.durable_frame;
  CHECK(assembler.has_frame() && assembler.has_reservation() &&
        assembler.point_count() == 1U);
  CHECK(assembler.append(next_point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kPending);
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  CHECK(assembler.has_frame() && hash.calls == 2U);
  CHECK(assembler.handoff(sink) == AssemblerStatus::kOk);
  CHECK(sink.calls == 2U && sink.logical_commits == 1U &&
        sink.attempts.size() == 2U && sink.attempts[0] == sink.attempts[1] &&
        sink.durable_frame == first_frame);
  emit_frame("sha_retry", sink.durable_frame);

  CHECK(!assembler.has_frame() && !assembler.has_reservation());
  CHECK(assembler.append(next_point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  const ChunkReservation next_reservation = get_reservation(assembler);
  CHECK(next_reservation.chunk_sequence == 1U &&
        next_reservation.first_point_sequence == 1U &&
        next_reservation.point_count == 1U);
  MemorySink next_sink;
  CHECK(assembler.handoff(next_sink) == AssemblerStatus::kOk);
  emit_frame("sha_next", next_sink.durable_frame);
}

void test_missing_sha_does_not_reserve_identity() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  prepare_store(backend, store, kThirdDeviceBytes, boot);
  ChunkAssembler assembler(store, nullptr, nullptr);
  const TrackPointV3 point = moving_point(1750000250U);
  CHECK(assembler.append(point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  const unsigned writes = backend.writes;
  CHECK(assembler.seal(false) == AssemblerStatus::kEncodeFailure);
  CHECK(assembler.last_codec_status() == track::v3::Status::kShaUnavailable);
  CHECK(assembler.point_count() == 1U && !assembler.has_reservation() &&
        !assembler.has_frame() && backend.writes == writes);

  ChunkReservation next = {};
  CHECK(store.reserve_chunk(1U, next) == IdentityStatus::kOk);
  CHECK(next.chunk_sequence == 0U && next.first_point_sequence == 0U &&
        next.point_count == 1U && next.boot_sequence == boot.boot_sequence);
}

void test_explicit_gap_and_fixed_quality_boundaries() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  prepare_store(backend, store, kThirdDeviceBytes, boot);
  HashContext hash;
  ChunkAssembler assembler(store, python_sha256, &hash);

  const TrackPointV3 no_gap = fix_point(0, 0, 0U,
                                        track::v3::kSpeedUnavailable, 0U, 0U);
  CHECK(assembler.append(no_gap, TimeQuality::kUnknown) ==
        AssemblerStatus::kInvalidPoint);
  CHECK(assembler.point_count() == 0U && !assembler.has_reservation());
  const TrackPointV3 explicit_gap = gap_point();
  CHECK(assembler.append(explicit_gap, TimeQuality::kUnknown) ==
        AssemblerStatus::kOk);
  CHECK(assembler.point_count() == 1U);
  CHECK(assembler.seal(true) == AssemblerStatus::kOk);
  const ChunkReservation reservation = get_reservation(assembler);
  CHECK(reservation.chunk_sequence == 0U &&
        reservation.first_point_sequence == 0U &&
        reservation.point_count == 1U);
  MemorySink sink;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kOk);
  CHECK(sink.durable_frame.size() == track::v3::kChunkHeaderSize +
                                         track::v3::kPointSize);
  emit_frame("gap", sink.durable_frame);
}

void test_full_96_point_chunk_and_backpressure_boundary() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  prepare_store(backend, store, kDeviceBytes, boot);
  HashContext hash;
  ChunkAssembler assembler(store, python_sha256, &hash);
  TrackPointV3 points[track::identity::kMaxPointCountPerChunk];
  for (size_t i = 0U; i < track::identity::kMaxPointCountPerChunk; ++i) {
    const int32_t latitude = static_cast<int32_t>(47100000U + i);
    const int32_t longitude = static_cast<int32_t>(-740700000L +
                                                    static_cast<int32_t>(i));
    points[i] = fix_point(latitude, longitude, 1750001000U, 200U, 8U,
                          static_cast<uint8_t>(track::v3::kFixValid |
                                               track::v3::kTimeTrusted));
    CHECK(assembler.append(points[i], TimeQuality::kApproximatePersisted) ==
          AssemblerStatus::kOk);
    CHECK(assembler.point_count() == i + 1U);
  }
  const TrackPointV3 extra = fix_point(
      47100100, -740700100, 1750001000U, 200U, 8U,
      static_cast<uint8_t>(track::v3::kFixValid | track::v3::kTimeTrusted));
  CHECK(assembler.append(extra, TimeQuality::kApproximatePersisted) ==
        AssemblerStatus::kNeedsSeal);
  CHECK(assembler.point_count() == track::identity::kMaxPointCountPerChunk &&
        !assembler.has_reservation() && !assembler.has_frame());
  CHECK(assembler.seal(false) == AssemblerStatus::kOk);
  CHECK(assembler.point_count() == track::identity::kMaxPointCountPerChunk);
  CHECK(assembler.frame_size() == track::v3::kMaxEncodedChunkSize);
  CHECK(assembler.frame_size() <= track::v3::kMaxEncodedChunkSize);
  MemorySink sink;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kOk);
  CHECK(sink.durable_frame.size() == track::v3::kMaxEncodedChunkSize);
  emit_frame("max96", sink.durable_frame);
}

void test_final_handoff_is_terminal_for_boot() {
  MemoryBackend backend;
  DeviceIdentityStore store(backend);
  BootSession boot = {};
  prepare_store(backend, store, kSecondDeviceBytes, boot);
  HashContext hash;
  ChunkAssembler assembler(store, python_sha256, &hash);
  const TrackPointV3 point = moving_point(1750000300U);
  CHECK(assembler.append(point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kOk);
  CHECK(assembler.seal(true) == AssemblerStatus::kOk);
  MemorySink sink;
  sink.commit_then_reject = true;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kSinkRejected);
  CHECK(!assembler.finalized() && assembler.has_frame() &&
        assembler.has_reservation() && assembler.point_count() == 1U);
  CHECK(assembler.append(point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kPending);
  CHECK(assembler.seal(false) == AssemblerStatus::kInvalidArgument);
  CHECK(!assembler.finalized() && sink.calls == 1U &&
        sink.logical_commits == 1U);
  const std::vector<uint8_t> pending_final_frame = sink.durable_frame;
  CHECK(assembler.handoff(sink) == AssemblerStatus::kOk);
  CHECK(sink.calls == 2U && sink.logical_commits == 1U &&
        sink.attempts[0] == sink.attempts[1] &&
        sink.attempts[0] == pending_final_frame);
  CHECK(assembler.handoff(sink) == AssemblerStatus::kFinalized);
  emit_frame("final", sink.durable_frame);
  const unsigned hash_calls = hash.calls;
  const unsigned sink_calls = sink.calls;
  CHECK(assembler.finalized());
  CHECK(assembler.append(point, TimeQuality::kGnssTrusted) ==
        AssemblerStatus::kFinalized);
  CHECK(assembler.seal(false) == AssemblerStatus::kFinalized);
  CHECK(assembler.handoff(sink) == AssemblerStatus::kFinalized);
  CHECK(assembler.point_count() == 0U && !assembler.has_frame() &&
        !assembler.has_reservation());
  CHECK(hash.calls == hash_calls && sink.calls == sink_calls);
}

}  // namespace

int main() {
  test_pregrant_gating_and_no_storage_mutation();
  test_validation_retry_backpressure_reentry_and_next_sequence();
  test_failed_sha_keeps_one_reservation_and_same_frame_retry();
  test_missing_sha_does_not_reserve_identity();
  test_explicit_gap_and_fixed_quality_boundaries();
  test_full_96_point_chunk_and_backpressure_boundary();
  test_final_handoff_is_terminal_for_boot();
  std::cout << "PASS chunk_assembler_native checks=" << check_count << '\n';
  return 0;
}
