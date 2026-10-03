#include "track/track_v3_mbedtls.h"

#include <mbedtls/sha256.h>

namespace track {
namespace v3 {

bool mbedtls_sha256_callback(const uint8_t *data, size_t len,
                             uint8_t digest[32], void *context) {
  (void)context;
  if ((data == NULL && len != 0) || digest == NULL) {
    return false;
  }
  static const uint8_t kEmptyInput = 0;
  const uint8_t *input = data != NULL ? data : &kEmptyInput;
  return mbedtls_sha256(input, len, digest, 0) == 0;
}

} // namespace v3
} // namespace track
