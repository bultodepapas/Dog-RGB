#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class Preferences {
 public:
  std::map<std::string, std::vector<uint8_t>> blobs;
  unsigned writes = 0;
  enum Fault { None, Reject, Truncate, Corrupt, Readback } fault = None;
  bool fail_read = false;
  size_t getBytesLength(const char *key) { return blobs[key].size(); }
  size_t getBytes(const char *key, void *out, size_t size) {
    if (fail_read) { fail_read = false; return 0; }
    const auto &bytes = blobs[key];
    if (bytes.size() != size) return 0;
    memcpy(out, bytes.data(), size); return size;
  }
  size_t putBytes(const char *key, const void *input, size_t size) {
    ++writes;
    if (fault == Reject) return 0;
    const auto *bytes = static_cast<const uint8_t *>(input);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + (fault == Truncate ? size / 2 : size));
    if (fault == Corrupt) blobs[key][20] ^= 0x80;
    if (fault == Readback) fail_read = true;
    return blobs[key].size();
  }
};
