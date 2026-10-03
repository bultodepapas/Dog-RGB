#pragma once

#include "track/device_identity.h"
#include <nvs.h>

namespace track {
namespace identity {

// Explicitly opened after the application initializes the default NVS partition.
// Own one handle and one Store on the same serialized application task.
// Never initializes, erases or repairs a partition. No constructor I/O.
class NvsBackend final : public Backend {
 public:
  NvsBackend();
  ~NvsBackend() override;
  NvsBackend(const NvsBackend &) = delete;
  NvsBackend &operator=(const NvsBackend &) = delete;

  bool open();
  void close();
  bool is_open() const;
  ReadStatus read(uint8_t slot, uint8_t *out, size_t capacity,
                  size_t &actual_size) override;
  bool write(uint8_t slot, const uint8_t *data, size_t size) override;

 private:
  nvs_handle_t handle_;
  bool opened_;
};

} // namespace identity
} // namespace track
