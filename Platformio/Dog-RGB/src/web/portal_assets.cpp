#include "web/portal_assets.h"

#include <WebServer.h>
#include <pgmspace.h>

namespace portal_assets {
namespace {

int quality(const String &parameters) {
  int start = 0;
  while (start < static_cast<int>(parameters.length())) {
    int end = parameters.indexOf(';', start);
    if (end < 0) end = parameters.length();
    String parameter = parameters.substring(start, end);
    parameter.trim();
    parameter.toLowerCase();
    if (parameter.startsWith("q=")) {
      String value = parameter.substring(2);
      value.trim();
      if (value == "1") return 1000;
      if (value.startsWith("1.")) {
        for (size_t i = 2; i < value.length(); ++i) {
          if (value[i] != '0') return 0;
        }
        return 1000;
      }
      if (value == "0") return 0;
      if (!value.startsWith("0.") || value.length() < 3) return 0;
      int result = 0;
      int scale = 100;
      for (size_t i = 2; i < value.length() && scale > 0; ++i) {
        if (!isDigit(value[i])) return 0;
        result += (value[i] - '0') * scale;
        scale /= 10;
      }
      return result;
    }
    start = end + 1;
  }
  return 1000;
}

bool etag_matches(const String &header, const char *etag) {
  int start = 0;
  while (start <= static_cast<int>(header.length())) {
    int end = header.indexOf(',', start);
    if (end < 0) end = header.length();
    String candidate = header.substring(start, end);
    candidate.trim();
    if (candidate == "*") return true;
    if (candidate.startsWith("W/")) candidate.remove(0, 2);
    if (candidate == etag) return true;
    if (end == static_cast<int>(header.length())) break;
    start = end + 1;
  }
  return false;
}

} // namespace

bool accepts_gzip(const String &value, bool header_present) {
  if (!header_present) return true;
  String header = value;
  header.trim();
  if (header.length() == 0) return false;

  int gzip_quality = -1;
  int wildcard_quality = -1;
  int start = 0;
  while (start <= static_cast<int>(header.length())) {
    int end = header.indexOf(',', start);
    if (end < 0) end = header.length();
    String entry = header.substring(start, end);
    entry.trim();
    const int semicolon = entry.indexOf(';');
    String coding = semicolon < 0 ? entry : entry.substring(0, semicolon);
    coding.trim();
    coding.toLowerCase();
    const int q = semicolon < 0 ? 1000 : quality(entry.substring(semicolon + 1));
    if (coding == "gzip" && q > gzip_quality) gzip_quality = q;
    if (coding == "*" && q > wildcard_quality) wildcard_quality = q;
    if (end == static_cast<int>(header.length())) break;
    start = end + 1;
  }

  if (gzip_quality >= 0) return gzip_quality > 0;
  return wildcard_quality > 0;
}

void send(WebServer &server, const WebAsset &asset) {
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Vary", "Accept-Encoding");

  const bool has_accept_encoding = server.hasHeader("Accept-Encoding");
  if (!accepts_gzip(server.header("Accept-Encoding"), has_accept_encoding)) {
    server.send(406, "text/plain; charset=utf-8",
                "Este portal requiere un cliente compatible con gzip.\n");
    return;
  }

  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("ETag", asset.etag);
  if (server.hasHeader("If-None-Match") &&
      etag_matches(server.header("If-None-Match"), asset.etag)) {
    server.send(304, asset.content_type, "");
    return;
  }

  server.send_P(200, asset.content_type,
                reinterpret_cast<PGM_P>(asset.gzip_data), asset.gzip_size);
}

} // namespace portal_assets
