#pragma once

#include <stddef.h>
#include <stdint.h>

namespace util {

// Standard CRC-32/ISO-HDLC (polynomial 0x04C11DB7, reflected form).
// This matches the widely used "CRC-32/IEEE" value produced by zlib.
inline uint32_t crc32_ieee(const void *data, size_t len) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

} // namespace util
