#pragma once

#include <stddef.h>
#include <stdint.h>

#include "led/scene.h"

namespace led {

enum class SceneOrigin : uint8_t {
  None = 0,
  Builtin = 1,
  User = 2,
};

class SceneCatalog {
 public:
  SceneCatalog();

  const SceneV1 *find(uint8_t scene_id) const;
  const SceneV1 &builtin_at(size_t index) const;
  const SceneV1 *user_at(uint8_t slot_one_based) const;
  bool user_occupied(uint8_t slot_one_based) const;
  const SceneBank &user_bank() const;
  uint32_t generation() const;

  bool replace_users(const SceneBank &bank, uint32_t generation);
  void clear_users(uint32_t generation = 0);
  uint8_t show_eligible_ids(uint8_t *out, size_t capacity) const;

 private:
  SceneBank users_;
  uint32_t generation_;
};

SceneOrigin scene_origin(uint8_t scene_id);
const char *scene_origin_name(SceneOrigin origin);
const char *scene_key(uint8_t scene_id);

} // namespace led
