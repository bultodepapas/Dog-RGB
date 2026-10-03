#include "track/chunk_assembler.h"

#include <string.h>

namespace track {
namespace v3 {
namespace {

static_assert(identity::kMaxPointCountPerChunk == kMaxPointsPerChunk,
              "identity and V3 chunk capacities must match");

bool is_usable_quality(TimeQuality quality, uint32_t utc_s) {
  const uint8_t raw = static_cast<uint8_t>(quality);
  if (raw == static_cast<uint8_t>(TimeQuality::kUnknown)) return utc_s == 0U;
  return raw >= static_cast<uint8_t>(TimeQuality::kApproximatePersisted) &&
         raw <= static_cast<uint8_t>(TimeQuality::kGnssTrusted) &&
         utc_s != 0U;
}

bool is_legacy_point(const TrackPointV3 &point) {
  return (point.flags & kLegacyV2) != 0U;
}

bool is_fix(const TrackPointV3 &point) {
  return (point.flags & kFixValid) != 0U;
}

bool is_gap(const TrackPointV3 &point) {
  return (point.flags & kGap) != 0U;
}

}  // namespace

ChunkAssembler::ChunkAssembler(identity::DeviceIdentityStore &store,
                               Sha256Fn sha256, void *sha256_context)
    : store_(store), sha256_(sha256), sha256_context_(sha256_context),
      points_{}, point_count_(0U), quality_(TimeQuality::kUnknown),
      frozen_(false), final_for_recording_(false), finalized_(false),
      have_reservation_(false), reservation_{}, staging_{}, frame_{},
      frame_size_(0U), busy_(false), last_status_(AssemblerStatus::kOk),
      last_identity_status_(identity::Status::kNotMounted),
      last_codec_status_(Status::kOk) {}

AssemblerStatus ChunkAssembler::set_status(AssemblerStatus status) {
  last_status_ = status;
  return status;
}

AssemblerStatus ChunkAssembler::append(const TrackPointV3 &point,
                                      TimeQuality quality) {
  if (busy_) return AssemblerStatus::kBusy;
  busy_ = true;

  if (finalized_) {
    busy_ = false;
    return set_status(AssemblerStatus::kFinalized);
  }
  if (frozen_) {
    busy_ = false;
    return set_status(AssemblerStatus::kPending);
  }

  last_identity_status_ = store_.status();
  if (!store_.session_active()) {
    busy_ = false;
    return set_status(AssemblerStatus::kNoActiveSession);
  }
  if (last_identity_status_ != identity::Status::kOk) {
    busy_ = false;
    return set_status(AssemblerStatus::kIdentityFailure);
  }

  if (!is_usable_quality(quality, point.utc_s)) {
    busy_ = false;
    return set_status(AssemblerStatus::kInvalidTimeQuality);
  }

  uint8_t encoded_point[kPointSize] = {};
  last_codec_status_ = encode_point(point, encoded_point, sizeof(encoded_point));
  if (last_codec_status_ != Status::kOk || is_legacy_point(point) ||
      (!is_fix(point) && !is_gap(point))) {
    busy_ = false;
    return set_status(AssemblerStatus::kInvalidPoint);
  }

  if (point_count_ != 0U &&
      (quality != quality_ ||
       (point.utc_s != 0U &&
        point.utc_s < points_[point_count_ - 1U].utc_s) ||
       point_count_ >= identity::kMaxPointCountPerChunk)) {
    busy_ = false;
    return set_status(AssemblerStatus::kNeedsSeal);
  }

  if (point_count_ == 0U) {
    quality_ = quality;
  }
  points_[point_count_] = point;
  ++point_count_;

  busy_ = false;
  return set_status(AssemblerStatus::kOk);
}

AssemblerStatus ChunkAssembler::seal(bool final_for_recording) {
  if (busy_) return AssemblerStatus::kBusy;
  busy_ = true;

  if (finalized_) {
    busy_ = false;
    return set_status(AssemblerStatus::kFinalized);
  }
  if (point_count_ == 0U) {
    busy_ = false;
    return set_status(AssemblerStatus::kEmpty);
  }
  if (frozen_ && final_for_recording != final_for_recording_) {
    busy_ = false;
    return set_status(AssemblerStatus::kInvalidArgument);
  }
  if (frame_size_ != 0U) {
    busy_ = false;
    return set_status(AssemblerStatus::kOk);
  }

  if (!have_reservation_ && sha256_ == nullptr) {
    last_codec_status_ = Status::kShaUnavailable;
    busy_ = false;
    return set_status(AssemblerStatus::kEncodeFailure);
  }

  if (!have_reservation_) {
    last_identity_status_ = store_.status();
    if (!store_.session_active()) {
      busy_ = false;
      return set_status(AssemblerStatus::kNoActiveSession);
    }
    if (last_identity_status_ != identity::Status::kOk) {
      busy_ = false;
      return set_status(AssemblerStatus::kIdentityFailure);
    }

    identity::ChunkReservation reservation = {};
    last_identity_status_ = store_.reserve_chunk(
        static_cast<uint32_t>(point_count_), reservation);
    if (last_identity_status_ != identity::Status::kOk) {
      busy_ = false;
      return set_status(last_identity_status_ ==
                                identity::Status::kNoActiveSession
                            ? AssemblerStatus::kNoActiveSession
                            : AssemblerStatus::kIdentityFailure);
    }

    reservation_ = reservation;
    have_reservation_ = true;
    frozen_ = true;
    final_for_recording_ = final_for_recording;
  }

  ChunkV3 chunk = {};
  memcpy(chunk.device_id, reservation_.device_id.bytes,
         sizeof(chunk.device_id));
  chunk.boot_sequence = reservation_.boot_sequence;
  chunk.chunk_sequence = reservation_.chunk_sequence;
  chunk.first_point_sequence = reservation_.first_point_sequence;
  chunk.time_quality = quality_;
  chunk.final_for_recording = final_for_recording_;
  chunk.points = points_;
  chunk.point_count = point_count_;

  size_t encoded_len = 0U;
  last_codec_status_ = encode_chunk(
      chunk, frame_, sizeof(frame_), staging_, sizeof(staging_), sha256_,
      sha256_context_, &encoded_len);
  if (last_codec_status_ != Status::kOk) {
    busy_ = false;
    return set_status(AssemblerStatus::kEncodeFailure);
  }

  frame_size_ = encoded_len;
  busy_ = false;
  return set_status(AssemblerStatus::kOk);
}

AssemblerStatus ChunkAssembler::handoff(ChunkSink &sink) {
  if (busy_) return AssemblerStatus::kBusy;
  if (finalized_) return set_status(AssemblerStatus::kFinalized);
  if (frame_size_ == 0U) {
    return set_status(point_count_ == 0U ? AssemblerStatus::kEmpty
                                        : AssemblerStatus::kPending);
  }

  busy_ = true;
  const bool accepted = sink.accept(frame_, frame_size_);
  busy_ = false;
  if (!accepted) return set_status(AssemblerStatus::kSinkRejected);

  const bool accepted_final = final_for_recording_;
  memset(points_, 0, sizeof(points_));
  memset(&reservation_, 0, sizeof(reservation_));
  point_count_ = 0U;
  quality_ = TimeQuality::kUnknown;
  frozen_ = false;
  final_for_recording_ = false;
  have_reservation_ = false;
  frame_size_ = 0U;
  finalized_ = accepted_final;
  last_codec_status_ = Status::kOk;
  return set_status(AssemblerStatus::kOk);
}

size_t ChunkAssembler::point_count() const { return point_count_; }

size_t ChunkAssembler::frame_size() const { return frame_size_; }

bool ChunkAssembler::has_frame() const { return frame_size_ != 0U; }

bool ChunkAssembler::has_reservation() const { return have_reservation_; }

bool ChunkAssembler::finalized() const { return finalized_; }

bool ChunkAssembler::reservation(identity::ChunkReservation &out) const {
  if (!have_reservation_) return false;
  out = reservation_;
  return true;
}

AssemblerStatus ChunkAssembler::last_status() const { return last_status_; }

identity::Status ChunkAssembler::last_identity_status() const {
  return last_identity_status_;
}

Status ChunkAssembler::last_codec_status() const { return last_codec_status_; }

}  // namespace v3
}  // namespace track
