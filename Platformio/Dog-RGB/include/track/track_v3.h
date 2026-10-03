#pragma once

#include <stddef.h>
#include <stdint.h>

namespace track {
namespace v3 {

static const size_t kPointSize = 16;
static const size_t kChunkHeaderSize = 92;
static const size_t kMaxPointsPerChunk = 96;
static const size_t kMaxEncodedChunkSize =
    kChunkHeaderSize + kMaxPointsPerChunk * kPointSize;
static const uint16_t kSpeedUnavailable = 0xFFFFU;

enum class Status : uint8_t {
  kOk = 0,
  kInvalidArgument,
  kBufferTooSmall,
  kInvalidPoint,
  kInvalidChunk,
  kUnknownTimeQuality,
  kBadMagic,
  kUnsupportedVersion,
  kReservedBits,
  kInvalidLength,
  kPointSequenceOverflow,
  kHeaderCrcMismatch,
  kPayloadCrcMismatch,
  kPayloadShaMismatch,
  kTimeBoundsMismatch,
  kShaUnavailable,
  kShaFailure,
  kOverlappingBuffers,
};

enum class TimeQuality : uint8_t {
  kUnknown = 0,
  kApproximatePersisted = 1,
  kServerAnchored = 2,
  kSntpSynced = 3,
  kGnssTrusted = 4,
  kLegacyMinute = 5,
};

enum PointFlag : uint8_t {
  kFixValid = 0x01,
  kMovementEvidence = 0x02,
  kTimeTrusted = 0x04,
  kStationaryHeartbeat = 0x08,
  kLowQuality = 0x10,
  kGap = 0x20,
  kLegacyV2 = 0x40,
};

enum ChunkFlag : uint16_t {
  kFinalForRecording = 0x0001,
};

struct TrackPointV3 {
  int32_t lat_e7;
  int32_t lon_e7;
  uint32_t utc_s;
  uint16_t speed_cmps;
  uint8_t satellites;
  uint8_t flags;
};

// UUID bytes use the RFC 4122/network byte order from uuid.UUID.bytes.
struct ChunkV3 {
  uint8_t device_id[16];
  uint32_t boot_sequence;
  uint32_t chunk_sequence;
  uint32_t first_point_sequence;
  TimeQuality time_quality;
  bool final_for_recording;
  const TrackPointV3 *points;
  size_t point_count;
};

// Hashes exactly the supplied bytes and writes 32 digest bytes on success.
// Implementations must not mutate data, retain pointers, or modify caller
// buffers other than digest during the call.
typedef bool (*Sha256Fn)(const uint8_t *data, size_t len,
                         uint8_t digest[32], void *context);

Status encode_point(const TrackPointV3 &point, uint8_t *out, size_t out_cap);
Status decode_point(const uint8_t *data, size_t len, TrackPointV3 *out);

// staging must hold the complete frame. staging, out and encoded_len must be
// pairwise disjoint and must not overlap chunk or chunk.points. Staging may
// change on failure; out and encoded_len are published only on success.
Status encode_chunk(const ChunkV3 &chunk, uint8_t *out, size_t out_cap,
                    uint8_t *staging, size_t staging_cap, Sha256Fn sha256,
                    void *sha256_context, size_t *encoded_len);

// All output pointers are required. The input frame must stay unchanged until
// return; points_out must hold point_count. Outputs must be pairwise disjoint
// and must not overlap data. Output arguments remain untouched unless
// Status::kOk is returned.
Status decode_chunk(const uint8_t *data, size_t len,
                    TrackPointV3 *points_out, size_t points_cap,
                    ChunkV3 *chunk_out, uint32_t *payload_crc32_out,
                    uint8_t payload_sha256_out[32], Sha256Fn sha256,
                    void *sha256_context);

} // namespace v3
} // namespace track
