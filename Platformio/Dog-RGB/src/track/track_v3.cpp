#include "track/track_v3.h"

#include "util/crc32.h"

#include <stdint.h>
#include <string.h>

namespace track {
namespace v3 {
namespace {

const uint8_t kSchemaVersion = 3;
const uint8_t kHeaderVersion = 1;
const uint8_t kMagic[4] = {'D', '3', 'C', 'K'};
const uint8_t kKnownPointFlags = 0x7FU;
const uint16_t kKnownChunkFlags = kFinalForRecording;
const size_t kPayloadShaOffset = 56;
const size_t kHeaderCrcOffset = 88;

uint16_t read_u16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t read_u32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

int32_t read_i32(const uint8_t *data) {
  const uint32_t bits = read_u32(data);
  if (bits <= 0x7FFFFFFFUL) {
    return static_cast<int32_t>(bits);
  }
  return static_cast<int32_t>(-1 - static_cast<int64_t>(~bits));
}

void write_u16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8U);
}

void write_u32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8U);
  data[2] = static_cast<uint8_t>(value >> 16U);
  data[3] = static_cast<uint8_t>(value >> 24U);
}

void encode_point_unchecked(const TrackPointV3 &point, uint8_t *out) {
  write_u32(out, static_cast<uint32_t>(point.lat_e7));
  write_u32(out + 4, static_cast<uint32_t>(point.lon_e7));
  write_u32(out + 8, point.utc_s);
  write_u16(out + 12, point.speed_cmps);
  out[14] = point.satellites;
  out[15] = point.flags;
}

TrackPointV3 decode_point_unchecked(const uint8_t *data) {
  TrackPointV3 point;
  point.lat_e7 = read_i32(data);
  point.lon_e7 = read_i32(data + 4);
  point.utc_s = read_u32(data + 8);
  point.speed_cmps = read_u16(data + 12);
  point.satellites = data[14];
  point.flags = data[15];
  return point;
}

Status validate_point(const TrackPointV3 &point) {
  if ((point.flags & static_cast<uint8_t>(~kKnownPointFlags)) != 0) {
    return Status::kInvalidPoint;
  }

  const bool has_fix = (point.flags & kFixValid) != 0;
  const bool is_gap = (point.flags & kGap) != 0;
  const bool moving = (point.flags & kMovementEvidence) != 0;
  const bool stationary = (point.flags & kStationaryHeartbeat) != 0;
  const bool trusted_time = (point.flags & kTimeTrusted) != 0;

  if (moving && stationary) {
    return Status::kInvalidPoint;
  }
  if (is_gap && (has_fix || moving || stationary)) {
    return Status::kInvalidPoint;
  }
  if (trusted_time != (point.utc_s != 0)) {
    return Status::kInvalidPoint;
  }

  if (has_fix) {
    if (point.lat_e7 < -900000000L || point.lat_e7 > 900000000L ||
        point.lon_e7 < -1800000000L || point.lon_e7 > 1800000000L) {
      return Status::kInvalidPoint;
    }
  } else if (point.lat_e7 != 0 || point.lon_e7 != 0 ||
             point.speed_cmps != kSpeedUnavailable) {
    return Status::kInvalidPoint;
  }

  return Status::kOk;
}

bool valid_time_quality(uint8_t value) { return value <= 5U; }

bool uuid_is_nil(const uint8_t device_id[16]) {
  uint8_t any = 0;
  for (size_t i = 0; i < 16; ++i) {
    any = static_cast<uint8_t>(any | device_id[i]);
  }
  return any == 0;
}

struct TimeBounds {
  uint32_t start;
  uint32_t end;
  bool has_timestamp;
};

struct ChunkPointRules {
  bool unknown_time;
  bool legacy_quality;
  bool legacy_boot;
  bool have_previous_timestamp;
  uint32_t previous_timestamp;
  TimeBounds bounds;
};

Status begin_chunk_point_validation(const uint8_t device_id[16],
                                    uint32_t boot_sequence,
                                    TimeQuality time_quality,
                                    ChunkPointRules *rules) {
  if (uuid_is_nil(device_id)) {
    return Status::kInvalidChunk;
  }
  const uint8_t quality = static_cast<uint8_t>(time_quality);
  if (!valid_time_quality(quality)) {
    return Status::kUnknownTimeQuality;
  }
  if (rules == NULL) {
    return Status::kInvalidArgument;
  }

  rules->unknown_time = time_quality == TimeQuality::kUnknown;
  rules->legacy_quality = time_quality == TimeQuality::kLegacyMinute;
  rules->legacy_boot = boot_sequence == 0;
  rules->have_previous_timestamp = false;
  rules->previous_timestamp = 0;
  rules->bounds.start = 0;
  rules->bounds.end = 0;
  rules->bounds.has_timestamp = false;
  return Status::kOk;
}

Status validate_and_accumulate_point(const TrackPointV3 &point,
                                     ChunkPointRules *rules) {
  if (rules == NULL) {
    return Status::kInvalidArgument;
  }
  const Status point_status = validate_point(point);
  if (point_status != Status::kOk) {
    return point_status;
  }

  const bool timestamped = point.utc_s != 0;
  const bool legacy_point = (point.flags & kLegacyV2) != 0;
  if (rules->unknown_time && timestamped) {
    return Status::kInvalidChunk;
  }
  if (!rules->unknown_time && !timestamped) {
    return Status::kInvalidChunk;
  }
  if (rules->legacy_boot) {
    if (!rules->legacy_quality || !legacy_point) {
      return Status::kInvalidChunk;
    }
  } else if (legacy_point) {
    return Status::kInvalidChunk;
  }
  if (rules->legacy_quality != legacy_point) {
    return Status::kInvalidChunk;
  }

  if (timestamped) {
    if (rules->have_previous_timestamp &&
        point.utc_s < rules->previous_timestamp) {
      return Status::kInvalidChunk;
    }
    if (!rules->bounds.has_timestamp) {
      rules->bounds.start = point.utc_s;
      rules->bounds.has_timestamp = true;
    }
    rules->bounds.end = point.utc_s;
    rules->previous_timestamp = point.utc_s;
    rules->have_previous_timestamp = true;
  }
  return Status::kOk;
}

Status validate_chunk_points(const uint8_t device_id[16],
                             uint32_t boot_sequence, TimeQuality time_quality,
                             const TrackPointV3 *points, size_t point_count,
                             TimeBounds *bounds) {
  if (points == NULL || bounds == NULL || point_count == 0 ||
      point_count > kMaxPointsPerChunk) {
    return Status::kInvalidChunk;
  }
  ChunkPointRules rules;
  Status status = begin_chunk_point_validation(device_id, boot_sequence,
                                                time_quality, &rules);
  if (status != Status::kOk) {
    return status;
  }
  for (size_t i = 0; i < point_count; ++i) {
    status = validate_and_accumulate_point(points[i], &rules);
    if (status != Status::kOk) {
      return status;
    }
  }
  *bounds = rules.bounds;
  return Status::kOk;
}

Status validate_wire_points(const uint8_t *payload, size_t point_count,
                            const uint8_t device_id[16],
                            uint32_t boot_sequence, TimeQuality time_quality,
                            TimeBounds *bounds) {
  if (payload == NULL || bounds == NULL || point_count == 0 ||
      point_count > kMaxPointsPerChunk) {
    return Status::kInvalidChunk;
  }
  ChunkPointRules rules;
  Status status = begin_chunk_point_validation(device_id, boot_sequence,
                                                time_quality, &rules);
  if (status != Status::kOk) {
    return status;
  }
  for (size_t i = 0; i < point_count; ++i) {
    const TrackPointV3 point = decode_point_unchecked(payload + i * kPointSize);
    status = validate_and_accumulate_point(point, &rules);
    if (status != Status::kOk) {
      return status;
    }
  }
  *bounds = rules.bounds;
  return Status::kOk;
}

bool ranges_overlap(const void *left, size_t left_len, const void *right,
                    size_t right_len) {
  if (left_len == 0 || right_len == 0) {
    return false;
  }
  const uintptr_t left_start = reinterpret_cast<uintptr_t>(left);
  const uintptr_t right_start = reinterpret_cast<uintptr_t>(right);
  return left_start <= right_start ? (right_start - left_start < left_len)
                                   : (left_start - right_start < right_len);
}

bool overlaps_any_output(const uint8_t *data, size_t len,
                         const TrackPointV3 *points_out, size_t points_bytes,
                         const ChunkV3 *chunk_out, const uint32_t *crc_out,
                         const uint8_t sha_out[32]) {
  return ranges_overlap(data, len, points_out, points_bytes) ||
         ranges_overlap(data, len, chunk_out, sizeof(*chunk_out)) ||
         ranges_overlap(data, len, crc_out, sizeof(*crc_out)) ||
         ranges_overlap(data, len, sha_out, 32);
}

bool outputs_overlap(const TrackPointV3 *points_out, size_t points_bytes,
                     const ChunkV3 *chunk_out, const uint32_t *crc_out,
                     const uint8_t sha_out[32]) {
  return ranges_overlap(points_out, points_bytes, chunk_out,
                        sizeof(*chunk_out)) ||
         ranges_overlap(points_out, points_bytes, crc_out, sizeof(*crc_out)) ||
         ranges_overlap(points_out, points_bytes, sha_out, 32) ||
         ranges_overlap(chunk_out, sizeof(*chunk_out), crc_out,
                        sizeof(*crc_out)) ||
         ranges_overlap(chunk_out, sizeof(*chunk_out), sha_out, 32) ||
         ranges_overlap(crc_out, sizeof(*crc_out), sha_out, 32);
}

} // namespace

Status encode_point(const TrackPointV3 &point, uint8_t *out, size_t out_cap) {
  if (out == NULL) {
    return Status::kInvalidArgument;
  }
  if (out_cap < kPointSize) {
    return Status::kBufferTooSmall;
  }
  const Status point_status = validate_point(point);
  if (point_status != Status::kOk) {
    return point_status;
  }

  uint8_t encoded[kPointSize];
  encode_point_unchecked(point, encoded);
  memcpy(out, encoded, sizeof(encoded));
  return Status::kOk;
}

Status decode_point(const uint8_t *data, size_t len, TrackPointV3 *out) {
  if (data == NULL || out == NULL) {
    return Status::kInvalidArgument;
  }
  if (len != kPointSize) {
    return Status::kInvalidLength;
  }

  const TrackPointV3 decoded = decode_point_unchecked(data);
  const Status point_status = validate_point(decoded);
  if (point_status != Status::kOk) {
    return point_status;
  }
  *out = decoded;
  return Status::kOk;
}

Status encode_chunk(const ChunkV3 &chunk, uint8_t *out, size_t out_cap,
                    uint8_t *staging, size_t staging_cap, Sha256Fn sha256,
                    void *sha256_context, size_t *encoded_len) {
  if (out == NULL || staging == NULL || chunk.points == NULL ||
      encoded_len == NULL) {
    return Status::kInvalidArgument;
  }
  if (chunk.point_count == 0 || chunk.point_count > kMaxPointsPerChunk) {
    return Status::kInvalidChunk;
  }
  const size_t payload_len = chunk.point_count * kPointSize;
  const size_t total_len = kChunkHeaderSize + payload_len;
  if (out_cap < total_len || staging_cap < total_len) {
    return Status::kBufferTooSmall;
  }
  if (sha256 == NULL) {
    return Status::kShaUnavailable;
  }
  const size_t points_bytes = chunk.point_count * sizeof(TrackPointV3);
  if (ranges_overlap(&chunk, sizeof(chunk), chunk.points, points_bytes) ||
      ranges_overlap(out, total_len, staging, total_len) ||
      ranges_overlap(staging, total_len, &chunk, sizeof(chunk)) ||
      ranges_overlap(staging, total_len, chunk.points, points_bytes) ||
      ranges_overlap(out, total_len, &chunk, sizeof(chunk)) ||
      ranges_overlap(out, total_len, chunk.points, points_bytes) ||
      ranges_overlap(out, total_len, encoded_len, sizeof(*encoded_len)) ||
      ranges_overlap(staging, total_len, encoded_len, sizeof(*encoded_len)) ||
      ranges_overlap(encoded_len, sizeof(*encoded_len), &chunk,
                     sizeof(chunk)) ||
      ranges_overlap(encoded_len, sizeof(*encoded_len), chunk.points,
                     points_bytes)) {
    return Status::kOverlappingBuffers;
  }
  if (chunk.first_point_sequence >
      UINT32_MAX - static_cast<uint32_t>(chunk.point_count - 1)) {
    return Status::kPointSequenceOverflow;
  }

  TimeBounds bounds;
  const Status semantic_status =
      validate_chunk_points(chunk.device_id, chunk.boot_sequence,
                            chunk.time_quality, chunk.points,
                            chunk.point_count, &bounds);
  if (semantic_status != Status::kOk) {
    return semantic_status;
  }

  for (size_t i = 0; i < chunk.point_count; ++i) {
    encode_point_unchecked(chunk.points[i], staging + kChunkHeaderSize +
                                                 i * kPointSize);
  }
  const uint8_t *payload = staging + kChunkHeaderSize;
  const uint32_t payload_crc = util::crc32_ieee(payload, payload_len);
  uint8_t payload_sha[32];
  if (!sha256(payload, payload_len, payload_sha, sha256_context)) {
    return Status::kShaFailure;
  }

  memset(staging, 0, kChunkHeaderSize);
  memcpy(staging, kMagic, sizeof(kMagic));
  staging[4] = kSchemaVersion;
  staging[5] = kHeaderVersion;
  write_u16(staging + 6,
            chunk.final_for_recording ? kFinalForRecording : 0U);
  memcpy(staging + 8, chunk.device_id, 16);
  write_u32(staging + 24, chunk.boot_sequence);
  write_u32(staging + 28, chunk.chunk_sequence);
  write_u32(staging + 32, chunk.first_point_sequence);
  write_u16(staging + 36, static_cast<uint16_t>(chunk.point_count));
  staging[38] = static_cast<uint8_t>(chunk.time_quality);
  staging[39] = 0;
  write_u32(staging + 40, bounds.start);
  write_u32(staging + 44, bounds.end);
  write_u32(staging + 48, static_cast<uint32_t>(payload_len));
  write_u32(staging + 52, payload_crc);
  memcpy(staging + kPayloadShaOffset, payload_sha, sizeof(payload_sha));
  write_u32(staging + kHeaderCrcOffset, 0);
  write_u32(staging + kHeaderCrcOffset,
            util::crc32_ieee(staging, kChunkHeaderSize));

  memcpy(out, staging, total_len);
  *encoded_len = total_len;
  return Status::kOk;
}

Status decode_chunk(const uint8_t *data, size_t len,
                    TrackPointV3 *points_out, size_t points_cap,
                    ChunkV3 *chunk_out, uint32_t *payload_crc32_out,
                    uint8_t payload_sha256_out[32], Sha256Fn sha256,
                    void *sha256_context) {
  if (data == NULL || points_out == NULL || chunk_out == NULL ||
      payload_crc32_out == NULL || payload_sha256_out == NULL) {
    return Status::kInvalidArgument;
  }
  if (len < kChunkHeaderSize + kPointSize) {
    return Status::kInvalidLength;
  }
  if (sha256 == NULL) {
    return Status::kShaUnavailable;
  }

  if (memcmp(data, kMagic, sizeof(kMagic)) != 0) {
    return Status::kBadMagic;
  }
  if (data[4] != kSchemaVersion || data[5] != kHeaderVersion) {
    return Status::kUnsupportedVersion;
  }
  const uint16_t flags = read_u16(data + 6);
  if ((flags & static_cast<uint16_t>(~kKnownChunkFlags)) != 0) {
    return Status::kReservedBits;
  }
  if (data[39] != 0) {
    return Status::kReservedBits;
  }

  const uint16_t point_count = read_u16(data + 36);
  if (point_count == 0 || point_count > kMaxPointsPerChunk) {
    return Status::kInvalidLength;
  }
  const uint32_t first_point_sequence = read_u32(data + 32);
  if (first_point_sequence >
      UINT32_MAX - static_cast<uint32_t>(point_count - 1U)) {
    return Status::kPointSequenceOverflow;
  }
  const uint8_t quality_raw = data[38];
  if (!valid_time_quality(quality_raw)) {
    return Status::kUnknownTimeQuality;
  }

  const uint32_t payload_len_raw = read_u32(data + 48);
  const size_t expected_payload_len =
      static_cast<size_t>(point_count) * kPointSize;
  if (payload_len_raw != expected_payload_len ||
      len != kChunkHeaderSize + expected_payload_len) {
    return Status::kInvalidLength;
  }
  const size_t points_bytes =
      static_cast<size_t>(point_count) * sizeof(TrackPointV3);
  if (points_cap < point_count) {
    return Status::kBufferTooSmall;
  }
  if (overlaps_any_output(data, len, points_out, points_bytes, chunk_out,
                          payload_crc32_out, payload_sha256_out) ||
      outputs_overlap(points_out, points_bytes, chunk_out,
                      payload_crc32_out, payload_sha256_out)) {
    return Status::kOverlappingBuffers;
  }

  uint8_t header[kChunkHeaderSize];
  memcpy(header, data, sizeof(header));
  const uint32_t stored_header_crc = read_u32(header + kHeaderCrcOffset);
  write_u32(header + kHeaderCrcOffset, 0);
  if (util::crc32_ieee(header, sizeof(header)) != stored_header_crc) {
    return Status::kHeaderCrcMismatch;
  }

  const uint8_t *payload = data + kChunkHeaderSize;
  const uint32_t stored_payload_crc = read_u32(data + 52);
  if (util::crc32_ieee(payload, expected_payload_len) != stored_payload_crc) {
    return Status::kPayloadCrcMismatch;
  }
  uint8_t calculated_sha[32];
  if (!sha256(payload, expected_payload_len, calculated_sha,
              sha256_context)) {
    return Status::kShaFailure;
  }
  if (memcmp(calculated_sha, data + kPayloadShaOffset,
             sizeof(calculated_sha)) != 0) {
    return Status::kPayloadShaMismatch;
  }

  uint8_t device_id[16];
  memcpy(device_id, data + 8, sizeof(device_id));
  const uint32_t boot_sequence = read_u32(data + 24);
  const TimeQuality time_quality = static_cast<TimeQuality>(quality_raw);
  TimeBounds bounds;
  const Status semantic_status =
      validate_wire_points(payload, point_count, device_id, boot_sequence,
                           time_quality, &bounds);
  if (semantic_status != Status::kOk) {
    return semantic_status;
  }
  if (read_u32(data + 40) != bounds.start ||
      read_u32(data + 44) != bounds.end) {
    return Status::kTimeBoundsMismatch;
  }

  // The immutable input has passed the complete validation pass. The second
  // pass cannot fail for a stable frame, so no public output is written early.
  for (size_t i = 0; i < point_count; ++i) {
    points_out[i] = decode_point_unchecked(payload + i * kPointSize);
  }
  ChunkV3 decoded_chunk;
  memcpy(decoded_chunk.device_id, device_id, sizeof(device_id));
  decoded_chunk.boot_sequence = boot_sequence;
  decoded_chunk.chunk_sequence = read_u32(data + 28);
  decoded_chunk.first_point_sequence = first_point_sequence;
  decoded_chunk.time_quality = time_quality;
  decoded_chunk.final_for_recording = (flags & kFinalForRecording) != 0;
  decoded_chunk.points = points_out;
  decoded_chunk.point_count = point_count;
  *chunk_out = decoded_chunk;
  *payload_crc32_out = stored_payload_crc;
  memcpy(payload_sha256_out, data + kPayloadShaOffset, 32);
  return Status::kOk;
}

} // namespace v3
} // namespace track
