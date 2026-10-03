#pragma once

#include "track/track_v3.h"

namespace track {
namespace v3 {

// SHA-256 callback backed by the ESP32 Arduino SDK's Mbed TLS library.
// Pass this function as Sha256Fn on ESP32; context is unused.
bool mbedtls_sha256_callback(const uint8_t *data, size_t len,
                             uint8_t digest[32], void *context);

} // namespace v3
} // namespace track
