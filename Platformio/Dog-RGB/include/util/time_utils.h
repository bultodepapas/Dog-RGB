#pragma once

#include <stdint.h>

// Wrap-safe helpers for Arduino's 32-bit millis() clock. Deadline comparisons
// require deadlines to be less than 2^31 ms (~24.8 days) away, which is far
// above every firmware timeout. Elapsed-time subtraction remains valid across
// one complete millis() wrap.
namespace time_utils {
constexpr uint32_t HALF_RANGE_MS = 0x80000000u;
constexpr uint32_t SAME_TICK_FUTURE_SKEW_MS = 1000u;

constexpr uint32_t elapsed_ms(uint32_t now_ms, uint32_t since_ms) {
  return now_ms - since_ms;
}

constexpr bool elapsed_at_least(uint32_t now_ms, uint32_t since_ms, uint32_t interval_ms) {
  return elapsed_ms(now_ms, since_ms) >= interval_ms;
}

constexpr bool elapsed_more_than(uint32_t now_ms, uint32_t since_ms, uint32_t interval_ms) {
  return elapsed_ms(now_ms, since_ms) > interval_ms;
}

constexpr bool elapsed_at_most(uint32_t now_ms, uint32_t since_ms, uint32_t interval_ms) {
  return elapsed_ms(now_ms, since_ms) <= interval_ms;
}

constexpr bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
  return elapsed_ms(now_ms, deadline_ms) < HALF_RANGE_MS;
}

constexpr bool deadline_pending(uint32_t now_ms, uint32_t deadline_ms) {
  return !deadline_reached(now_ms, deadline_ms);
}

constexpr bool deadline_later(uint32_t candidate_ms, uint32_t reference_ms) {
  return candidate_ms != reference_ms &&
         elapsed_ms(candidate_ms, reference_ms) < HALF_RANGE_MS;
}

constexpr uint32_t remaining_ms(uint32_t now_ms, uint32_t deadline_ms) {
  return deadline_pending(now_ms, deadline_ms) ? elapsed_ms(deadline_ms, now_ms) : 0u;
}

// main.cpp captures its loop timestamp before gps::tick(). A new observation
// can therefore be a few milliseconds ahead of that snapshot. Clamp only this
// bounded skew; a high timestamp followed by a low timestamp at millis() wrap
// is still reported with its correct small positive age.
constexpr uint32_t age_ms(uint32_t now_ms, uint32_t observed_ms) {
  return elapsed_ms(observed_ms, now_ms) > 0u &&
                 elapsed_ms(observed_ms, now_ms) <= SAME_TICK_FUTURE_SKEW_MS
             ? 0u
             : elapsed_ms(now_ms, observed_ms);
}
}  // namespace time_utils

static_assert(time_utils::elapsed_ms(5u, UINT32_MAX - 4u) == 10u,
              "elapsed time must cross millis() rollover");
static_assert(time_utils::elapsed_at_least(5u, UINT32_MAX - 4u, 10u),
              "elapsed thresholds must cross millis() rollover");
static_assert(time_utils::deadline_pending(UINT32_MAX - 10u, 5u),
              "a deadline after rollover must remain pending");
static_assert(time_utils::deadline_reached(5u, UINT32_MAX - 10u),
              "a wrapped deadline must become due");
static_assert(time_utils::age_ms(5u, UINT32_MAX - 4u) == 10u,
              "age must not confuse rollover with a future observation");
static_assert(time_utils::age_ms(100u, 103u) == 0u,
              "same-loop observation skew must clamp to zero");
