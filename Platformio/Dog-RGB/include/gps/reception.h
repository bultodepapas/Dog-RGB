#pragma once

#include <stdint.h>
#include "util/time_utils.h"

namespace gps {
// Shared by expiry in the parser's tick and the diagnostic/read-only view.
constexpr uint32_t kRmcStaleMs = 3000;
constexpr uint32_t kUartStaleMs = 5000;

enum class ReceptionState : uint8_t { NoData, Receiving, Searching, Untrusted, Fix, Stale };
struct ReceptionInput {
  uint32_t now_ms;
  bool byte_observed;
  uint32_t last_byte_ms;
  bool rmc_observed;
  uint32_t last_rmc_ms;
  bool raw_fix;
  bool trusted_fix;
};

constexpr bool uart_stale(uint32_t now, bool observed, uint32_t last) {
  return observed && time_utils::elapsed_more_than(now, last, kUartStaleMs);
}
constexpr bool rmc_stale(uint32_t now, bool observed, uint32_t last) {
  return observed && time_utils::elapsed_more_than(now, last, kRmcStaleMs);
}
constexpr ReceptionState classify_reception(const ReceptionInput &input) {
  if (!input.byte_observed) return ReceptionState::NoData;
  if (uart_stale(input.now_ms, input.byte_observed, input.last_byte_ms) ||
      rmc_stale(input.now_ms, input.rmc_observed, input.last_rmc_ms)) {
    return ReceptionState::Stale;
  }
  if (!input.rmc_observed) return ReceptionState::Receiving;
  if (input.trusted_fix) return ReceptionState::Fix;
  return input.raw_fix ? ReceptionState::Untrusted : ReceptionState::Searching;
}
constexpr const char *reception_name(ReceptionState state) {
  switch (state) {
    case ReceptionState::NoData: return "no-data";
    case ReceptionState::Receiving: return "bytes-no-rmc";
    case ReceptionState::Searching: return "searching";
    case ReceptionState::Untrusted: return "untrusted";
    case ReceptionState::Fix: return "fix";
    case ReceptionState::Stale: return "stale";
  }
  return "unknown";
}
} // namespace gps
