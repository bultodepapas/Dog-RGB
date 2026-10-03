#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "track/track_v3.h"

namespace {

using track::v3::ChunkV3;
using track::v3::Status;
using track::v3::TimeQuality;
using track::v3::TrackPointV3;

const size_t kMaxInputBytes = 8192;
const size_t kMaxInputPoints = 97;

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk:
    return "ok";
  case Status::kInvalidArgument:
    return "invalid_argument";
  case Status::kBufferTooSmall:
    return "buffer_too_small";
  case Status::kInvalidPoint:
    return "invalid_point";
  case Status::kInvalidChunk:
    return "invalid_chunk";
  case Status::kUnknownTimeQuality:
    return "unknown_time_quality";
  case Status::kBadMagic:
    return "bad_magic";
  case Status::kUnsupportedVersion:
    return "unsupported_version";
  case Status::kReservedBits:
    return "reserved_bits";
  case Status::kInvalidLength:
    return "invalid_length";
  case Status::kPointSequenceOverflow:
    return "point_sequence_overflow";
  case Status::kHeaderCrcMismatch:
    return "header_crc_mismatch";
  case Status::kPayloadCrcMismatch:
    return "payload_crc_mismatch";
  case Status::kPayloadShaMismatch:
    return "payload_sha_mismatch";
  case Status::kTimeBoundsMismatch:
    return "time_bounds_mismatch";
  case Status::kShaUnavailable:
    return "sha_unavailable";
  case Status::kShaFailure:
    return "sha_failure";
  case Status::kOverlappingBuffers:
    return "overlapping_buffers";
  }
  return "unknown_status";
}

int hex_nibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool decode_hex(const std::string &text, uint8_t *out, size_t cap,
                size_t *out_len) {
  if (out_len == nullptr || text.size() % 2U != 0U ||
      text.size() / 2U > cap) {
    return false;
  }
  const size_t length = text.size() / 2U;
  for (size_t i = 0; i < length; ++i) {
    const int high = hex_nibble(text[i * 2U]);
    const int low = hex_nibble(text[i * 2U + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>((high << 4) | low);
  }
  *out_len = length;
  return true;
}

std::string encode_hex(const uint8_t *data, size_t len) {
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(len * 2U);
  for (size_t i = 0; i < len; ++i) {
    result[i * 2U] = digits[(data[i] >> 4U) & 0x0FU];
    result[i * 2U + 1U] = digits[data[i] & 0x0FU];
  }
  return result;
}

bool read_point(std::istringstream &input, TrackPointV3 *point) {
  int64_t latitude = 0;
  int64_t longitude = 0;
  uint64_t utc = 0;
  uint64_t speed = 0;
  uint64_t satellites = 0;
  uint64_t flags = 0;
  if (point == nullptr ||
      !(input >> latitude >> longitude >> utc >> speed >> satellites >> flags)) {
    return false;
  }
  if (latitude < std::numeric_limits<int32_t>::min() ||
      latitude > std::numeric_limits<int32_t>::max() ||
      longitude < std::numeric_limits<int32_t>::min() ||
      longitude > std::numeric_limits<int32_t>::max() ||
      utc > std::numeric_limits<uint32_t>::max() ||
      speed > std::numeric_limits<uint16_t>::max() ||
      satellites > std::numeric_limits<uint8_t>::max() ||
      flags > std::numeric_limits<uint8_t>::max()) {
    return false;
  }
  point->lat_e7 = static_cast<int32_t>(latitude);
  point->lon_e7 = static_cast<int32_t>(longitude);
  point->utc_s = static_cast<uint32_t>(utc);
  point->speed_cmps = static_cast<uint16_t>(speed);
  point->satellites = static_cast<uint8_t>(satellites);
  point->flags = static_cast<uint8_t>(flags);
  return true;
}

TrackPointV3 sentinel_point() {
  TrackPointV3 point = {};
  point.lat_e7 = static_cast<int32_t>(0x31323334);
  point.lon_e7 = static_cast<int32_t>(0x41424344);
  point.utc_s = 0x51525354U;
  point.speed_cmps = 0x6162U;
  point.satellites = 0x71U;
  point.flags = 0x72U;
  return point;
}

bool same_point(const TrackPointV3 &left, const TrackPointV3 &right) {
  return left.lat_e7 == right.lat_e7 && left.lon_e7 == right.lon_e7 &&
         left.utc_s == right.utc_s && left.speed_cmps == right.speed_cmps &&
         left.satellites == right.satellites && left.flags == right.flags;
}

void print_point(const TrackPointV3 &point) {
  std::cout << ' ' << point.lat_e7 << ' ' << point.lon_e7 << ' ' << point.utc_s
            << ' ' << static_cast<unsigned int>(point.speed_cmps) << ' '
            << static_cast<unsigned int>(point.satellites) << ' '
            << static_cast<unsigned int>(point.flags);
}

struct HashContext {
  std::istream *input;
  std::ostream *output;
};

bool python_sha256(const uint8_t *data, size_t len, uint8_t digest[32],
                   void *context) {
  if (data == nullptr || digest == nullptr || context == nullptr) {
    return false;
  }
  HashContext *hash_context = static_cast<HashContext *>(context);
  *hash_context->output << "SHA " << encode_hex(data, len) << '\n';
  hash_context->output->flush();

  std::string response;
  if (!std::getline(*hash_context->input, response)) {
    return false;
  }
  const std::string prefix("HASH ");
  if (response.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  uint8_t parsed[32] = {};
  size_t parsed_len = 0;
  if (!decode_hex(response.substr(prefix.size()), parsed, sizeof(parsed),
                  &parsed_len) ||
      parsed_len != sizeof(parsed)) {
    return false;
  }
  std::memcpy(digest, parsed, sizeof(parsed));
  return true;
}

bool read_device_id(const std::string &text, uint8_t device_id[16]) {
  size_t length = 0;
  return decode_hex(text, device_id, 16U, &length) && length == 16U;
}

bool bytes_equal(const uint8_t *data, size_t len, uint8_t value) {
  return std::all_of(data, data + len,
                     [value](uint8_t current) { return current == value; });
}

bool same_chunk(const ChunkV3 &left, const ChunkV3 &right) {
  for (size_t i = 0; i < sizeof(left.device_id); ++i) {
    if (left.device_id[i] != right.device_id[i]) {
      return false;
    }
  }
  return left.boot_sequence == right.boot_sequence &&
         left.chunk_sequence == right.chunk_sequence &&
         left.first_point_sequence == right.first_point_sequence &&
         left.time_quality == right.time_quality &&
         left.final_for_recording == right.final_for_recording &&
         left.points == right.points && left.point_count == right.point_count;
}

TrackPointV3 alias_fixture_point(uint32_t utc_s) {
  TrackPointV3 point = {};
  point.lat_e7 = 46500000;
  point.lon_e7 = -740600000;
  point.utc_s = utc_s;
  point.speed_cmps = 140U;
  point.satellites = 10U;
  point.flags = static_cast<uint8_t>(track::v3::kFixValid |
                                    track::v3::kTimeTrusted |
                                    track::v3::kMovementEvidence);
  return point;
}

ChunkV3 alias_fixture_chunk(const TrackPointV3 *points) {
  ChunkV3 chunk = {};
  const uint8_t device_id[16] = {0x00U, 0x11U, 0x22U, 0x33U,
                                 0x44U, 0x55U, 0x46U, 0x77U,
                                 0x88U, 0x99U, 0xAAU, 0xBBU,
                                 0xCCU, 0xDDU, 0xEEU, 0xFFU};
  std::memcpy(chunk.device_id, device_id, sizeof(device_id));
  chunk.boot_sequence = 7U;
  chunk.chunk_sequence = 9U;
  chunk.first_point_sequence = 40U;
  chunk.time_quality = TimeQuality::kGnssTrusted;
  chunk.final_for_recording = true;
  chunk.points = points;
  chunk.point_count = 2U;
  return chunk;
}

struct alignas(ChunkV3) ChunkStorage {
  uint8_t bytes[track::v3::kMaxEncodedChunkSize + sizeof(ChunkV3) + 8U];
};

struct alignas(TrackPointV3) PointStorage {
  TrackPointV3 points[2];
  uint8_t padding[track::v3::kMaxEncodedChunkSize -
                  2U * sizeof(TrackPointV3)];
};

struct alignas(size_t) SizeStorage {
  uint8_t bytes[track::v3::kMaxEncodedChunkSize];
};

void handle_aliases(std::istringstream &input, HashContext *hash_context) {
  std::string blob_hex;
  if (!(input >> blob_hex)) {
    std::cout << "RESULT A protocol_error\n";
    return;
  }
  uint8_t wire[8192] = {};
  size_t wire_len = 0;
  if (!decode_hex(blob_hex, wire, sizeof(wire), &wire_len) ||
      wire_len < track::v3::kChunkHeaderSize + track::v3::kPointSize) {
    std::cout << "RESULT A protocol_error\n";
    return;
  }

  const TrackPointV3 original_points[2] = {
      alias_fixture_point(1750000000U), alias_fixture_point(1750000001U)};
  const size_t required = track::v3::kChunkHeaderSize +
                          2U * track::v3::kPointSize;
  const size_t encoded_len_sentinel =
      std::numeric_limits<size_t>::max() - static_cast<size_t>(0x5678U);
  size_t cases = 0;
  size_t failure_case = 0;
  const char *failure_status = "none";

  const auto verify = [&](Status status, bool unchanged) {
    ++cases;
    if ((status != Status::kOverlappingBuffers || !unchanged) &&
        failure_case == 0U) {
      failure_case = cases;
      failure_status = status_name(status);
    }
  };

  // Partial overlap between output and staging must be rejected before SHA or
  // any caller-owned bytes are changed.
  {
    TrackPointV3 points[2] = {original_points[0], original_points[1]};
    ChunkV3 chunk = alias_fixture_chunk(points);
    const ChunkV3 before = chunk;
    uint8_t storage[2U * track::v3::kMaxEncodedChunkSize];
    std::fill(storage, storage + sizeof(storage), 0xA5U);
    size_t encoded_len = encoded_len_sentinel;
    const Status status = track::v3::encode_chunk(
        chunk, storage, required, storage + 1U, required, python_sha256,
        hash_context, &encoded_len);
    verify(status, same_chunk(chunk, before) &&
                       bytes_equal(storage, sizeof(storage), 0xA5U) &&
                       encoded_len == encoded_len_sentinel);
  }

  // Staging overlapping the descriptor and output overlapping the descriptor.
  {
    TrackPointV3 points[2] = {original_points[0], original_points[1]};
    ChunkV3 source = alias_fixture_chunk(points);
    ChunkStorage storage = {};
    std::fill(storage.bytes, storage.bytes + sizeof(storage.bytes), 0xA5U);
    ChunkV3 *chunk = new (storage.bytes) ChunkV3(source);
    const ChunkV3 before = *chunk;
    uint8_t out[track::v3::kMaxEncodedChunkSize];
    std::fill(out, out + sizeof(out), 0xA5U);
    size_t encoded_len = encoded_len_sentinel;
    Status status = track::v3::encode_chunk(
        *chunk, out, required, reinterpret_cast<uint8_t *>(chunk), required,
        python_sha256, hash_context, &encoded_len);
    verify(status, same_chunk(*chunk, before) &&
                       bytes_equal(out, sizeof(out), 0xA5U) &&
                       encoded_len == encoded_len_sentinel);

    ChunkStorage output_storage = {};
    std::fill(output_storage.bytes,
              output_storage.bytes + sizeof(output_storage.bytes), 0xA5U);
    chunk = new (output_storage.bytes) ChunkV3(source);
    const ChunkV3 output_before = *chunk;
    uint8_t staging[track::v3::kMaxEncodedChunkSize];
    std::fill(staging, staging + sizeof(staging), 0x5AU);
    encoded_len = encoded_len_sentinel;
    status = track::v3::encode_chunk(
        *chunk, reinterpret_cast<uint8_t *>(chunk), required, staging,
        required, python_sha256, hash_context, &encoded_len);
    verify(status, same_chunk(*chunk, output_before) &&
                       bytes_equal(staging, sizeof(staging), 0x5AU) &&
                       encoded_len == encoded_len_sentinel);
  }

  // Output or staging overlapping source points must preserve both points.
  {
    PointStorage storage = {};
    TrackPointV3 *points = storage.points;
    points[0] = original_points[0];
    points[1] = original_points[1];
    const TrackPointV3 before_points[2] = {points[0], points[1]};
    ChunkV3 chunk = alias_fixture_chunk(points);
    const ChunkV3 before_chunk = chunk;
    uint8_t staging[track::v3::kMaxEncodedChunkSize];
    std::fill(staging, staging + sizeof(staging), 0x5AU);
    size_t encoded_len = encoded_len_sentinel;
    Status status = track::v3::encode_chunk(
        chunk, reinterpret_cast<uint8_t *>(points), required, staging,
        required, python_sha256, hash_context, &encoded_len);
    verify(status, same_chunk(chunk, before_chunk) &&
                       same_point(points[0], before_points[0]) &&
                       same_point(points[1], before_points[1]) &&
                       encoded_len == encoded_len_sentinel);

    points[0] = original_points[0];
    points[1] = original_points[1];
    const TrackPointV3 stage_before[2] = {points[0], points[1]};
    chunk = alias_fixture_chunk(points);
    const ChunkV3 stage_chunk_before = chunk;
    uint8_t out[track::v3::kMaxEncodedChunkSize];
    std::fill(out, out + sizeof(out), 0xA5U);
    encoded_len = encoded_len_sentinel;
    status = track::v3::encode_chunk(
        chunk, out, required, reinterpret_cast<uint8_t *>(points), required,
        python_sha256, hash_context, &encoded_len);
    verify(status, same_chunk(chunk, stage_chunk_before) &&
                       same_point(points[0], stage_before[0]) &&
                       same_point(points[1], stage_before[1]) &&
                       bytes_equal(out, sizeof(out), 0xA5U) &&
                       encoded_len == encoded_len_sentinel);
  }

  // encoded_len may not alias the descriptor, its source points, or output.
  {
    alignas(size_t) TrackPointV3 points[2] = {original_points[0],
                                               original_points[1]};
    ChunkV3 chunk = alias_fixture_chunk(points);
    const ChunkV3 before = chunk;
    uint8_t out[track::v3::kMaxEncodedChunkSize];
    uint8_t staging[track::v3::kMaxEncodedChunkSize];
    std::fill(out, out + sizeof(out), 0xA5U);
    std::fill(staging, staging + sizeof(staging), 0x5AU);
    Status status = track::v3::encode_chunk(
        chunk, out, required, staging, required, python_sha256, hash_context,
        &chunk.point_count);
    verify(status, same_chunk(chunk, before) &&
                       bytes_equal(out, sizeof(out), 0xA5U));

    SizeStorage output_and_len = {};
    std::fill(output_and_len.bytes,
              output_and_len.bytes + sizeof(output_and_len.bytes), 0xA5U);
    size_t *aliased_len = new (output_and_len.bytes) size_t(encoded_len_sentinel);
    const std::vector<uint8_t> before_bytes(
        output_and_len.bytes,
        output_and_len.bytes + sizeof(output_and_len.bytes));
    status = track::v3::encode_chunk(
        chunk, output_and_len.bytes, required, staging, required,
        python_sha256, hash_context, aliased_len);
    verify(status, *aliased_len == encoded_len_sentinel &&
                       std::equal(before_bytes.begin(), before_bytes.end(),
                                  output_and_len.bytes));

    status = track::v3::encode_chunk(
        chunk, out, required, staging, required, python_sha256, hash_context,
        reinterpret_cast<size_t *>(&points[0]));
    verify(status, same_point(points[0], original_points[0]) &&
                       same_point(points[1], original_points[1]) &&
                       same_chunk(chunk, before) &&
                       bytes_equal(out, sizeof(out), 0xA5U));
  }

  // A valid decode frame must reject input/output alias and overlapping output
  // ranges before invoking SHA or publishing any field.
  {
    struct alignas(TrackPointV3) InputStorage {
      uint8_t bytes[8192];
    } input_storage = {};
    std::memcpy(input_storage.bytes, wire, wire_len);
    const std::vector<uint8_t> wire_before(input_storage.bytes,
                                           input_storage.bytes + wire_len);
    TrackPointV3 *points_out =
        reinterpret_cast<TrackPointV3 *>(input_storage.bytes);
    ChunkV3 decoded = {};
    std::fill(decoded.device_id, decoded.device_id + sizeof(decoded.device_id),
              0xC3U);
    decoded.boot_sequence = 0xC3C3C3C3U;
    decoded.chunk_sequence = 0xD4D4D4D4U;
    decoded.first_point_sequence = 0xE5E5E5E5U;
    decoded.time_quality = static_cast<TimeQuality>(0xF1U);
    decoded.final_for_recording = true;
    decoded.points = nullptr;
    decoded.point_count = static_cast<size_t>(0xF2F2U);
    const ChunkV3 decoded_before = decoded;
    const uint32_t crc_before = 0xA1B2C3D4U;
    uint32_t crc = crc_before;
    uint8_t sha[32];
    std::fill(sha, sha + sizeof(sha), 0xB6U);
    const Status status = track::v3::decode_chunk(
        input_storage.bytes, wire_len, points_out, 96U, &decoded, &crc, sha,
        python_sha256, hash_context);
    verify(status, same_chunk(decoded, decoded_before) && crc == crc_before &&
                       bytes_equal(sha, sizeof(sha), 0xB6U) &&
                       std::equal(wire_before.begin(), wire_before.end(),
                                  input_storage.bytes));

    TrackPointV3 points[track::v3::kMaxPointsPerChunk];
    const TrackPointV3 point_before = sentinel_point();
    std::fill(points, points + track::v3::kMaxPointsPerChunk, point_before);
    ChunkV3 overlapping = {};
    std::fill(overlapping.device_id,
              overlapping.device_id + sizeof(overlapping.device_id), 0xC3U);
    overlapping.boot_sequence = 0xC3C3C3C3U;
    overlapping.chunk_sequence = 0xD4D4D4D4U;
    overlapping.first_point_sequence = 0xE5E5E5E5U;
    overlapping.time_quality = static_cast<TimeQuality>(0xF1U);
    overlapping.final_for_recording = true;
    overlapping.points = nullptr;
    overlapping.point_count = static_cast<size_t>(0xF2F2U);
    const ChunkV3 overlap_before = overlapping;
    crc = crc_before;
    std::fill(sha, sha + sizeof(sha), 0xB6U);
    TrackPointV3 *overlapping_points =
        reinterpret_cast<TrackPointV3 *>(overlapping.device_id);
    const Status output_alias_status = track::v3::decode_chunk(
        wire, wire_len, overlapping_points, 96U, &overlapping, &crc, sha,
        python_sha256, hash_context);
    verify(output_alias_status,
           same_chunk(overlapping, overlap_before) && crc == crc_before &&
               bytes_equal(sha, sizeof(sha), 0xB6U));

    decoded = decoded_before;
    crc = crc_before;
    std::fill(sha, sha + sizeof(sha), 0xB6U);
    uint32_t *crc_in_chunk = &decoded.boot_sequence;
    const Status metadata_crc_status = track::v3::decode_chunk(
        wire, wire_len, points, 96U, &decoded, crc_in_chunk, sha,
        python_sha256, hash_context);
    verify(metadata_crc_status,
           same_chunk(decoded, decoded_before) && crc == crc_before &&
               bytes_equal(sha, sizeof(sha), 0xB6U));

    for (size_t i = 0; i < track::v3::kMaxPointsPerChunk; ++i) {
      if (!same_point(points[i], point_before)) {
        failure_case = cases + 1U;
        failure_status = "decode_points_modified";
      }
    }
  }

  if (failure_case == 0U) {
    std::cout << "RESULT A ok " << cases << '\n';
  } else {
    std::cout << "RESULT A alias_failure " << failure_case << ' '
              << failure_status << ' ' << cases << '\n';
  }
  std::cout.flush();
}

void handle_encode(std::istringstream &input, HashContext *hash_context) {
  size_t out_cap = 0;
  size_t staging_cap = 0;
  unsigned int sha_mode = 0;
  std::string device_hex;
  uint32_t boot_sequence = 0;
  uint32_t chunk_sequence = 0;
  uint32_t first_point_sequence = 0;
  unsigned int time_quality = 0;
  unsigned int final_for_recording = 0;
  size_t point_count = 0;
  if (!(input >> out_cap >> staging_cap >> sha_mode >> device_hex >>
        boot_sequence >> chunk_sequence >> first_point_sequence >>
        time_quality >> final_for_recording >> point_count) ||
      point_count > kMaxInputPoints) {
    std::cout << "RESULT E protocol_error\n";
    return;
  }

  uint8_t device_id[16] = {};
  TrackPointV3 points[kMaxInputPoints] = {};
  bool valid = read_device_id(device_hex, device_id);
  for (size_t i = 0; i < point_count; ++i) {
    if (!read_point(input, &points[i])) {
      valid = false;
      break;
    }
  }
  if (!valid || sha_mode > 2U || time_quality > 255U ||
      final_for_recording > 1U) {
    std::cout << "RESULT E protocol_error\n";
    return;
  }

  ChunkV3 chunk = {};
  std::memcpy(chunk.device_id, device_id, sizeof(device_id));
  chunk.boot_sequence = boot_sequence;
  chunk.chunk_sequence = chunk_sequence;
  chunk.first_point_sequence = first_point_sequence;
  chunk.time_quality = static_cast<TimeQuality>(time_quality);
  chunk.final_for_recording = final_for_recording != 0U;
  chunk.points = points;
  chunk.point_count = point_count;

  uint8_t out[track::v3::kMaxEncodedChunkSize];
  uint8_t staging[track::v3::kMaxEncodedChunkSize];
  std::fill(out, out + sizeof(out), 0xA5U);
  std::fill(staging, staging + sizeof(staging), 0x5AU);
  const size_t encoded_len_sentinel =
      std::numeric_limits<size_t>::max() - static_cast<size_t>(0x1234U);
  size_t encoded_len = encoded_len_sentinel;
  const Status status = track::v3::encode_chunk(
      chunk, out, out_cap, staging, staging_cap,
      sha_mode == 2U ? nullptr : python_sha256,
      sha_mode == 2U ? nullptr : hash_context, &encoded_len);
  const bool out_unchanged =
      std::all_of(out, out + sizeof(out), [](uint8_t value) {
        return value == 0xA5U;
      });
  const bool len_unchanged = encoded_len == encoded_len_sentinel;
  std::cout << "RESULT E " << status_name(status) << ' ' << encoded_len << ' '
            << (out_unchanged ? 1 : 0) << ' ' << (len_unchanged ? 1 : 0);
  if (status == Status::kOk && encoded_len <= sizeof(out)) {
    std::cout << ' ' << encode_hex(out, encoded_len);
  }
  std::cout << '\n';
  std::cout.flush();
}

void handle_decode(std::istringstream &input, HashContext *hash_context) {
  size_t points_cap = 0;
  unsigned int sha_mode = 0;
  std::string blob_hex;
  if (!(input >> points_cap >> sha_mode >> blob_hex) || sha_mode > 2U ||
      blob_hex.size() > kMaxInputBytes * 2U) {
    std::cout << "RESULT D protocol_error\n";
    return;
  }

  uint8_t data[kMaxInputBytes] = {};
  size_t data_len = 0;
  if (!decode_hex(blob_hex, data, sizeof(data), &data_len)) {
    std::cout << "RESULT D protocol_error\n";
    return;
  }

  TrackPointV3 points[track::v3::kMaxPointsPerChunk];
  const TrackPointV3 point_sentinel = sentinel_point();
  std::fill(points, points + track::v3::kMaxPointsPerChunk, point_sentinel);
  ChunkV3 decoded = {};
  std::fill(decoded.device_id, decoded.device_id + sizeof(decoded.device_id),
            0xC3U);
  decoded.boot_sequence = 0xC3C3C3C3U;
  decoded.chunk_sequence = 0xD4D4D4D4U;
  decoded.first_point_sequence = 0xE5E5E5E5U;
  decoded.time_quality = static_cast<TimeQuality>(0xF1U);
  decoded.final_for_recording = true;
  decoded.points = nullptr;
  decoded.point_count = static_cast<size_t>(0xF2F2U);
  const ChunkV3 chunk_sentinel = decoded;
  uint32_t payload_crc32 = 0xA1B2C3D4U;
  uint8_t payload_sha256[32];
  std::fill(payload_sha256, payload_sha256 + sizeof(payload_sha256), 0xB6U);

  const Status status = track::v3::decode_chunk(
      data, data_len, points, points_cap, &decoded, &payload_crc32,
      payload_sha256, sha_mode == 2U ? nullptr : python_sha256,
      sha_mode == 2U ? nullptr : hash_context);

  bool points_unchanged = true;
  for (size_t i = 0; i < track::v3::kMaxPointsPerChunk; ++i) {
    if (!same_point(points[i], point_sentinel)) {
      points_unchanged = false;
      break;
    }
  }
  bool device_unchanged = true;
  for (size_t i = 0; i < sizeof(decoded.device_id); ++i) {
    if (decoded.device_id[i] != chunk_sentinel.device_id[i]) {
      device_unchanged = false;
      break;
    }
  }
  const bool metadata_unchanged =
      device_unchanged && decoded.boot_sequence == chunk_sentinel.boot_sequence &&
      decoded.chunk_sequence == chunk_sentinel.chunk_sequence &&
      decoded.first_point_sequence == chunk_sentinel.first_point_sequence &&
      decoded.time_quality == chunk_sentinel.time_quality &&
      decoded.final_for_recording == chunk_sentinel.final_for_recording &&
      decoded.points == chunk_sentinel.points &&
      decoded.point_count == chunk_sentinel.point_count;
  const bool crc_unchanged = payload_crc32 == 0xA1B2C3D4U;
  const bool sha_unchanged =
      std::all_of(payload_sha256, payload_sha256 + sizeof(payload_sha256),
                  [](uint8_t value) { return value == 0xB6U; });
  const bool outputs_unchanged = points_unchanged && metadata_unchanged &&
                                 crc_unchanged && sha_unchanged;

  std::cout << "RESULT D " << status_name(status) << ' '
            << (outputs_unchanged ? 1 : 0);
  if (status == Status::kOk) {
    std::cout << ' ' << encode_hex(decoded.device_id, sizeof(decoded.device_id))
              << ' ' << decoded.boot_sequence << ' ' << decoded.chunk_sequence
              << ' ' << decoded.first_point_sequence << ' '
              << static_cast<unsigned int>(decoded.time_quality) << ' '
              << (decoded.final_for_recording ? 1 : 0) << ' ' << payload_crc32
              << ' ' << encode_hex(payload_sha256, sizeof(payload_sha256)) << ' '
              << decoded.point_count << ' '
              << (decoded.points == points ? 1 : 0);
    for (size_t i = 0; i < decoded.point_count; ++i) {
      print_point(points[i]);
    }
  }
  std::cout << '\n';
  std::cout.flush();
}

void handle_encode_point(std::istringstream &input) {
  size_t out_cap = 0;
  TrackPointV3 point = {};
  if (!(input >> out_cap) || !read_point(input, &point)) {
    std::cout << "RESULT P protocol_error\n";
    return;
  }
  uint8_t out[track::v3::kPointSize];
  std::fill(out, out + sizeof(out), 0xA5U);
  const Status status = track::v3::encode_point(point, out, out_cap);
  const bool unchanged =
      std::all_of(out, out + sizeof(out), [](uint8_t value) {
        return value == 0xA5U;
      });
  std::cout << "RESULT P " << status_name(status) << ' '
            << (unchanged ? 1 : 0);
  if (status == Status::kOk) {
    std::cout << ' ' << encode_hex(out, sizeof(out));
  }
  std::cout << '\n';
  std::cout.flush();
}

void handle_decode_point(std::istringstream &input) {
  std::string point_hex;
  if (!(input >> point_hex) || point_hex.size() > kMaxInputBytes * 2U) {
    std::cout << "RESULT Q protocol_error\n";
    return;
  }
  uint8_t data[kMaxInputBytes] = {};
  size_t data_len = 0;
  if (!decode_hex(point_hex, data, sizeof(data), &data_len)) {
    std::cout << "RESULT Q protocol_error\n";
    return;
  }
  TrackPointV3 point = sentinel_point();
  const TrackPointV3 sentinel = point;
  const Status status = track::v3::decode_point(data, data_len, &point);
  const bool unchanged = same_point(point, sentinel);
  std::cout << "RESULT Q " << status_name(status) << ' '
            << (unchanged ? 1 : 0);
  if (status == Status::kOk) {
    print_point(point);
  }
  std::cout << '\n';
  std::cout.flush();
}

} // namespace

int main() {
  std::string line;
  HashContext hash_context = {&std::cin, &std::cout};
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string operation;
    if (!(input >> operation)) {
      std::cout << "RESULT protocol_error\n";
      std::cout.flush();
      continue;
    }
    if (operation == "QUIT") {
      std::cout << "BYE\n";
      std::cout.flush();
      return 0;
    }
    if (operation == "E") {
      handle_encode(input, &hash_context);
    } else if (operation == "D") {
      handle_decode(input, &hash_context);
    } else if (operation == "P") {
      handle_encode_point(input);
    } else if (operation == "Q") {
      handle_decode_point(input);
    } else if (operation == "A") {
      handle_aliases(input, &hash_context);
    } else {
      std::cout << "RESULT protocol_error\n";
      std::cout.flush();
    }
  }
  return 0;
}
