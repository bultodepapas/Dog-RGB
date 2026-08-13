#pragma once

#include <Arduino.h>

class WebServer;

namespace portal_assets {

struct WebAsset {
  const uint8_t *gzip_data;
  uint32_t gzip_size;
  uint32_t decoded_size;
  const char *content_type;
  const char *etag;
};

// The header_present flag matters: RFC 9110 treats a missing Accept-Encoding
// as accepting any coding, while an explicitly empty value accepts none.
bool accepts_gzip(const String &value, bool header_present);

// Sends one immutable gzip representation directly from flash. This function
// owns content negotiation, validators and cache headers; callers only record
// portal activity and select the asset.
void send(WebServer &server, const WebAsset &asset);

} // namespace portal_assets
