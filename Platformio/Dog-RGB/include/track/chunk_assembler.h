#pragma once

#include <stddef.h>
#include <stdint.h>

#include "track/device_identity.h"
#include "track/track_v3.h"

namespace track {
namespace v3 {

// Result of a bounded local chunk-assembly operation. Codec and identity
// details are available through the last_*_status() accessors.
enum class AssemblerStatus : uint8_t {
  kOk = 0,
  kNoActiveSession,
  kInvalidPoint,
  kInvalidTimeQuality,
  kNeedsSeal,
  kPending,
  kEmpty,
  kIdentityFailure,
  kEncodeFailure,
  kSinkRejected,
  kBusy,
  kInvalidArgument,
  kFinalized,
};

// A synchronous local-storage handoff. Returning true asserts that the exact
// frame was durably committed to verified local storage. It does not mean a
// server acknowledged or received it. Returning false, including an
// ambiguous storage result, leaves the frame and its identity reserved here;
// sinks must tolerate retrying the same identity and bytes idempotently.
class ChunkSink {
 public:
  virtual ~ChunkSink() {}
  virtual bool accept(const uint8_t *bytes, size_t size) = 0;
};

// Fixed-capacity V3 batch builder. Own one serialized instance for the whole
// boot session; the store and borrowed SHA context must outlive this
// assembler. Do not reconstruct it while a batch is pending or after the
// final latch, or that state can be lost. Construction performs no I/O. The
// object owns about 5 KiB, so keep it
// static or long-lived on constrained targets instead of placing it on a
// small stack. It is not thread-safe and cannot be copied or moved.
// Reentrant mutating calls from SHA/sink callbacks return kBusy.
class ChunkAssembler {
 public:
  ChunkAssembler(identity::DeviceIdentityStore &store, Sha256Fn sha256,
                 void *sha256_context);
  ChunkAssembler(const ChunkAssembler &) = delete;
  ChunkAssembler &operator=(const ChunkAssembler &) = delete;
  ChunkAssembler(ChunkAssembler &&) = delete;
  ChunkAssembler &operator=(ChunkAssembler &&) = delete;

  // A nonfix point must carry kGap, a stricter assembly policy than the wire
  // codec. Unknown time (quality 0) requires utc_s=0; known qualities 1..4
  // require nonzero utc_s. Legacy points and quality 5 are rejected. Equal
  // timestamps are allowed. A full batch, quality change, or backwards UTC
  // returns kNeedsSeal without consuming the incoming point. Appending while
  // frozen/sealed returns kPending; after an accepted final frame it returns
  // kFinalized.
  AssemblerStatus append(const TrackPointV3 &point, TimeQuality quality);

  // A missing SHA callback returns kEncodeFailure/kShaUnavailable before any
  // reservation. Otherwise reserve identity once, freeze the batch and
  // requested final flag, then encode. After a hash/encoding failure, retry
  // seal() with the same flag to use the same reservation. A different flag is
  // rejected. A successfully encoded frame is cached and repeat seal() does
  // not hash it again.
  AssemblerStatus seal(bool final_for_recording);

  // Calls sink.accept synchronously. Sink pointers are valid only during the
  // call; sinks must neither retain nor mutate them. On true, releases the
  // batch for another chunk unless it was final, in which case this assembler
  // is terminal for the boot session. On false, the exact frame and reservation
  // remain pending for retry. Exceptions are unsupported.
  AssemblerStatus handoff(ChunkSink &sink);

  size_t point_count() const;
  size_t frame_size() const;
  bool has_frame() const;
  bool has_reservation() const;
  bool finalized() const;
  bool reservation(identity::ChunkReservation &out) const;
  AssemblerStatus last_status() const;
  identity::Status last_identity_status() const;
  Status last_codec_status() const;

 private:
  AssemblerStatus set_status(AssemblerStatus status);

  identity::DeviceIdentityStore &store_;
  Sha256Fn sha256_;
  void *sha256_context_;
  TrackPointV3 points_[identity::kMaxPointCountPerChunk];
  size_t point_count_;
  TimeQuality quality_;
  bool frozen_;
  bool final_for_recording_;
  bool finalized_;
  bool have_reservation_;
  identity::ChunkReservation reservation_;
  uint8_t staging_[kMaxEncodedChunkSize];
  uint8_t frame_[kMaxEncodedChunkSize];
  size_t frame_size_;
  bool busy_;
  AssemblerStatus last_status_;
  identity::Status last_identity_status_;
  Status last_codec_status_;
};

}  // namespace v3
}  // namespace track
