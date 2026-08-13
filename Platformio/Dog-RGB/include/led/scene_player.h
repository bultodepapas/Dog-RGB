#pragma once

#include <stdint.h>

#include "led/scene_catalog.h"

namespace led {

enum class ScenePlayback : uint8_t {
  None = 0,
  Manual = 1,
  Show = 2,
};

enum class SceneCommand : uint8_t {
  None = 0,
  Apply = 1,
  Cancel = 2,
};

struct ScenePlayerDiagnostics {
  uint32_t apply_count;
  uint32_t cancel_count;
  uint32_t superseded_commands;
  uint32_t show_cycle_count;
  uint32_t lookup_failures;
};

class ScenePlayer {
 public:
  ScenePlayer(SceneCatalog &catalog, uint32_t show_duration_ms);

  void reset(uint32_t now_ms, uint8_t configured_mode);
  void seed(uint32_t seed_value);
  bool request_apply(uint8_t scene_id);
  void request_cancel();
  void tick(uint32_t now_ms, uint8_t configured_mode, bool show_mode,
            bool body_permitted);

  const SceneV1 *active_scene() const;
  uint8_t active_scene_id() const;
  ScenePlayback playback() const;
  const char *playback_name() const;
  SceneOrigin origin() const;
  uint8_t pending_scene_id() const;
  SceneCommand pending_command() const;
  bool stale() const;
  uint32_t applied_generation() const;
  uint32_t activation_revision() const;
  uint32_t show_elapsed_ms() const;
  const ScenePlayerDiagnostics &diagnostics() const;

 private:
  uint32_t next_random();
  void reset_show_bag();
  void build_show_bag();
  bool activate(uint8_t scene_id, ScenePlayback playback,
                uint32_t now_ms);
  bool activate_next_show(uint32_t now_ms);
  void clear_active();
  void refresh_stale();

  SceneCatalog &catalog_;
  uint32_t show_duration_ms_;
  SceneV1 active_;
  bool has_active_;
  ScenePlayback playback_;
  SceneCommand pending_command_;
  uint8_t pending_scene_id_;
  uint8_t configured_mode_;
  bool mode_initialized_;
  bool stale_;
  uint32_t applied_generation_;
  uint32_t checked_generation_;
  uint32_t activation_revision_;
  uint32_t last_tick_ms_;
  uint32_t show_elapsed_ms_;
  uint32_t random_state_;
  uint8_t show_ids_[SCENE_CATALOG_CAPACITY];
  uint8_t show_length_;
  uint8_t show_cursor_;
  uint8_t last_show_id_;
  ScenePlayerDiagnostics diagnostics_;
};

const char *scene_playback_name(ScenePlayback playback);

} // namespace led
