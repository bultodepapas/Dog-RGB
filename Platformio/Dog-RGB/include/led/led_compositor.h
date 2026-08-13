#pragma once

#include <stdint.h>

#include "led/led_frame.h"
#include "led/led_layout.h"

namespace led {

struct TransitionDiagnostics {
  bool active;
  uint16_t duration_ms;
  uint8_t progress;
  uint32_t started;
  uint32_t completed;
  uint32_t interrupted;
};

class LedCompositor {
 public:
  explicit LedCompositor(const LedLayoutConfig &layout_config);

  void begin_transition(const LedFrame &visible, uint32_t now_ms,
                        uint16_t duration_ms);
  void cancel_transition();
  void interrupt_for_alert();

  // `logical` stores status at [0..status_count) and each branch body at the
  // remaining logical indices. Mapping, mirroring, transition and alert
  // ownership happen here, after effects and before the power limiter.
  void compose(const LedFrame &logical, LedFrame &physical, uint32_t now_ms,
               bool mirror, bool status_enabled, bool alert_active,
               const Rgb &alert_color);

  const LedLayout &layout() const;
  const TransitionDiagnostics &diagnostics() const;

 private:
  void clear_frame(LedFrame &frame) const;
  void map_target(const LedFrame &logical, LedFrame &physical, bool mirror,
                  bool status_enabled);
  void blend_transition_body(LedFrame &physical, uint8_t amount) const;
  void apply_alert(LedFrame &physical, const Rgb &color) const;
  uint8_t transition_progress(uint32_t now_ms) const;

  LedLayout layout_;
  LedFrame transition_from_;
  uint32_t transition_started_ms_;
  TransitionDiagnostics diagnostics_;
};

} // namespace led
