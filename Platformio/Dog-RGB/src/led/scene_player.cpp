#include "led/scene_player.h"

#include <string.h>

namespace led {

ScenePlayer::ScenePlayer(SceneCatalog &catalog, uint32_t show_duration_ms)
    : catalog_(catalog),
      show_duration_ms_(show_duration_ms),
      active_{},
      has_active_(false),
      playback_(ScenePlayback::None),
      pending_command_(SceneCommand::None),
      pending_scene_id_(SCENE_ID_NONE),
      configured_mode_(0),
      mode_initialized_(false),
      stale_(false),
      applied_generation_(0),
      checked_generation_(0),
      activation_revision_(0),
      last_tick_ms_(0),
      show_elapsed_ms_(0),
      random_state_(UINT32_C(0x51CEB00C)),
      show_ids_{},
      show_length_(0),
      show_cursor_(0),
      last_show_id_(SCENE_ID_INVALID),
      diagnostics_{} {}

void ScenePlayer::reset(uint32_t now_ms, uint8_t configured_mode) {
  memset(&active_, 0, sizeof(active_));
  has_active_ = false;
  playback_ = ScenePlayback::None;
  pending_command_ = SceneCommand::None;
  pending_scene_id_ = SCENE_ID_NONE;
  configured_mode_ = configured_mode;
  mode_initialized_ = true;
  stale_ = false;
  applied_generation_ = 0;
  checked_generation_ = catalog_.generation();
  activation_revision_ = 0;
  last_tick_ms_ = now_ms;
  show_elapsed_ms_ = 0;
  last_show_id_ = SCENE_ID_INVALID;
  diagnostics_ = ScenePlayerDiagnostics{};
  reset_show_bag();
}

void ScenePlayer::seed(uint32_t seed_value) {
  random_state_ = seed_value == 0U ? UINT32_C(0x51CEB00C) : seed_value;
}

bool ScenePlayer::request_apply(uint8_t scene_id) {
  if (catalog_.find(scene_id) == nullptr) return false;
  if (pending_command_ != SceneCommand::None) {
    diagnostics_.superseded_commands++;
  }
  pending_command_ = SceneCommand::Apply;
  pending_scene_id_ = scene_id;
  return true;
}

void ScenePlayer::request_cancel() {
  if (pending_command_ != SceneCommand::None) {
    diagnostics_.superseded_commands++;
  }
  pending_command_ = SceneCommand::Cancel;
  pending_scene_id_ = SCENE_ID_NONE;
}

void ScenePlayer::tick(uint32_t now_ms, uint8_t configured_mode,
                       bool show_mode, bool body_permitted) {
  const uint32_t elapsed = now_ms - last_tick_ms_;
  last_tick_ms_ = now_ms;

  if (!mode_initialized_ || configured_mode != configured_mode_) {
    configured_mode_ = configured_mode;
    mode_initialized_ = true;
    if (playback_ == ScenePlayback::Manual) clear_active();
    reset_show_bag();
  }

  if (pending_command_ != SceneCommand::None) {
    const SceneCommand command = pending_command_;
    const uint8_t scene_id = pending_scene_id_;
    pending_command_ = SceneCommand::None;
    pending_scene_id_ = SCENE_ID_NONE;
    if (command == SceneCommand::Apply) {
      reset_show_bag();
      if (activate(scene_id, ScenePlayback::Manual, now_ms)) {
        diagnostics_.apply_count++;
      } else {
        diagnostics_.lookup_failures++;
        clear_active();
      }
    } else {
      clear_active();
      reset_show_bag();
      diagnostics_.cancel_count++;
    }
  }

  if (playback_ == ScenePlayback::Manual) {
    refresh_stale();
    return;
  }

  if (!show_mode) {
    if (playback_ == ScenePlayback::Show) clear_active();
    return;
  }

  if (playback_ != ScenePlayback::Show || !has_active_) {
    activate_next_show(now_ms);
    return;
  }

  refresh_stale();
  if (body_permitted) show_elapsed_ms_ += elapsed;
  if (show_elapsed_ms_ >= show_duration_ms_) {
    activate_next_show(now_ms);
  }
}

const SceneV1 *ScenePlayer::active_scene() const {
  return has_active_ ? &active_ : nullptr;
}

uint8_t ScenePlayer::active_scene_id() const {
  return has_active_ ? active_.scene_id : SCENE_ID_NONE;
}

ScenePlayback ScenePlayer::playback() const {
  return playback_;
}

const char *ScenePlayer::playback_name() const {
  return scene_playback_name(playback_);
}

SceneOrigin ScenePlayer::origin() const {
  return has_active_ ? scene_origin(active_.scene_id) : SceneOrigin::None;
}

uint8_t ScenePlayer::pending_scene_id() const {
  return pending_command_ == SceneCommand::Apply ? pending_scene_id_
                                                  : SCENE_ID_NONE;
}

SceneCommand ScenePlayer::pending_command() const {
  return pending_command_;
}

bool ScenePlayer::stale() const {
  return stale_;
}

uint32_t ScenePlayer::applied_generation() const {
  return applied_generation_;
}

uint32_t ScenePlayer::activation_revision() const {
  return activation_revision_;
}

uint32_t ScenePlayer::show_elapsed_ms() const {
  return show_elapsed_ms_;
}

const ScenePlayerDiagnostics &ScenePlayer::diagnostics() const {
  return diagnostics_;
}

uint32_t ScenePlayer::next_random() {
  uint32_t value = random_state_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  random_state_ = value == 0U ? UINT32_C(0x51CEB00C) : value;
  return random_state_;
}

void ScenePlayer::reset_show_bag() {
  memset(show_ids_, 0, sizeof(show_ids_));
  show_length_ = 0;
  show_cursor_ = 0;
  show_elapsed_ms_ = 0;
}

void ScenePlayer::build_show_bag() {
  show_length_ = catalog_.show_eligible_ids(show_ids_, sizeof(show_ids_));
  for (int i = static_cast<int>(show_length_) - 1; i > 0; --i) {
    const uint8_t j = static_cast<uint8_t>(next_random() %
                                           static_cast<uint32_t>(i + 1));
    const uint8_t temporary = show_ids_[i];
    show_ids_[i] = show_ids_[j];
    show_ids_[j] = temporary;
  }
  if (show_length_ > 1U && show_ids_[0] == last_show_id_) {
    const uint8_t swap_index = static_cast<uint8_t>(
        1U + next_random() % static_cast<uint32_t>(show_length_ - 1U));
    const uint8_t temporary = show_ids_[0];
    show_ids_[0] = show_ids_[swap_index];
    show_ids_[swap_index] = temporary;
  }
  show_cursor_ = 0;
  diagnostics_.show_cycle_count++;
}

bool ScenePlayer::activate(uint8_t scene_id, ScenePlayback playback,
                           uint32_t now_ms) {
  const SceneV1 *scene = catalog_.find(scene_id);
  if (scene == nullptr) return false;
  active_ = *scene;
  has_active_ = true;
  playback_ = playback;
  stale_ = false;
  applied_generation_ = catalog_.generation();
  checked_generation_ = applied_generation_;
  activation_revision_++;
  if (activation_revision_ == 0U) activation_revision_ = 1U;
  show_elapsed_ms_ = 0;
  last_tick_ms_ = now_ms;
  if (playback == ScenePlayback::Show) last_show_id_ = scene_id;
  return true;
}

bool ScenePlayer::activate_next_show(uint32_t now_ms) {
  for (uint8_t attempt = 0; attempt < SCENE_CATALOG_CAPACITY; ++attempt) {
    if (show_cursor_ >= show_length_) build_show_bag();
    if (show_length_ == 0U) {
      clear_active();
      return false;
    }
    const uint8_t scene_id = show_ids_[show_cursor_++];
    if (activate(scene_id, ScenePlayback::Show, now_ms)) return true;
    diagnostics_.lookup_failures++;
  }
  build_show_bag();
  if (show_length_ > 0U &&
      activate(show_ids_[show_cursor_++], ScenePlayback::Show, now_ms)) {
    return true;
  }
  diagnostics_.lookup_failures++;
  clear_active();
  return false;
}

void ScenePlayer::clear_active() {
  memset(&active_, 0, sizeof(active_));
  has_active_ = false;
  playback_ = ScenePlayback::None;
  stale_ = false;
  applied_generation_ = 0;
  checked_generation_ = catalog_.generation();
  show_elapsed_ms_ = 0;
}

void ScenePlayer::refresh_stale() {
  if (!has_active_ || checked_generation_ == catalog_.generation()) return;
  checked_generation_ = catalog_.generation();
  const SceneV1 *current = catalog_.find(active_.scene_id);
  stale_ = current == nullptr || !scene_semantic_equal(active_, *current);
}

const char *scene_playback_name(ScenePlayback playback) {
  switch (playback) {
    case ScenePlayback::Manual: return "manual";
    case ScenePlayback::Show: return "show";
    case ScenePlayback::None: return "none";
  }
  return "none";
}

} // namespace led
