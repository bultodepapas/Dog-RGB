#pragma once

#include "storage/scene_store.h"

namespace storage {

// Thin adapter only. Record selection, validation and recovery remain in the
// device-independent SceneStore so the exact same state machine is fault-tested
// on host.
class SceneNvsBackend final : public SceneRecordBackend {
 public:
  SceneBackendRead read(uint8_t bank_index, uint8_t *out, size_t capacity,
                        size_t &actual_size) override;
  SceneBackendWrite write(uint8_t bank_index, const uint8_t *data, size_t size,
                          size_t &written_size) override;
  size_t free_entries() const override;
};

} // namespace storage
