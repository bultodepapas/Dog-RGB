#include "led/scene_catalog.h"

#include <string.h>

#include "led/palette_registry.h"

namespace led {
namespace {

static const SceneV1 BUILTINS[SCENE_BUILTIN_COUNT] = {
    {1, true, true, 3, 3, PALETTE_SAFETY_AMBER, PALETTE_SAFETY_AMBER,
     120, 180, 255, 400, {255, 80, 0}, {255, 220, 160},
     "Alta visibilidad"},
    {2, true, true, 2, 2, PALETTE_NIGHT_RED, PALETTE_NIGHT_RED,
     45, 100, 110, 900, {120, 0, 0}, {255, 40, 10}, "Calmado"},
    {3, true, true, 4, 4, PALETTE_FOREST, PALETTE_FOREST,
     140, 170, 200, 500, {0, 90, 25}, {100, 255, 170}, "Activo"},
    {4, true, true, 9, 9, PALETTE_PRIDE, PALETTE_PRIDE,
     150, 180, 180, 650, {200, 0, 200}, {0, 200, 255}, "Fiesta"},
};

static const char *const BUILTIN_KEYS[SCENE_BUILTIN_COUNT] = {
    "high_visibility", "calm", "active", "party"};
static const char *const USER_KEYS[SCENE_USER_SLOT_COUNT] = {
    "user_1", "user_2", "user_3", "user_4"};

} // namespace

SceneCatalog::SceneCatalog() : users_{}, generation_(0) {
  scene_bank_clear(users_);
}

const SceneV1 *SceneCatalog::find(uint8_t scene_id) const {
  if (scene_id_is_builtin(scene_id)) {
    return &BUILTINS[scene_id - 1U];
  }
  const uint8_t slot = scene_slot_from_id(scene_id);
  return slot == 0U ? nullptr : user_at(slot);
}

const SceneV1 &SceneCatalog::builtin_at(size_t index) const {
  return BUILTINS[index < SCENE_BUILTIN_COUNT ? index : 0U];
}

const SceneV1 *SceneCatalog::user_at(uint8_t slot_one_based) const {
  if (!user_occupied(slot_one_based)) return nullptr;
  return &users_.slots[slot_one_based - 1U];
}

bool SceneCatalog::user_occupied(uint8_t slot_one_based) const {
  if (slot_one_based < 1U || slot_one_based > SCENE_USER_SLOT_COUNT) {
    return false;
  }
  const uint8_t bit = static_cast<uint8_t>(1U << (slot_one_based - 1U));
  return (users_.occupied_mask & bit) != 0U;
}

const SceneBank &SceneCatalog::user_bank() const {
  return users_;
}

uint32_t SceneCatalog::generation() const {
  return generation_;
}

bool SceneCatalog::replace_users(const SceneBank &bank, uint32_t generation) {
  if ((bank.occupied_mask & 0xF0U) != 0U) return false;
  SceneBank candidate = {};
  scene_bank_clear(candidate);
  candidate.occupied_mask = bank.occupied_mask;
  for (uint8_t i = 0; i < SCENE_USER_SLOT_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1U << i);
    if ((bank.occupied_mask & bit) == 0U) continue;
    if (bank.slots[i].scene_id != scene_id_from_slot(i + 1U) ||
        !scene_validate(bank.slots[i])) {
      return false;
    }
    candidate.slots[i] = bank.slots[i];
  }
  users_ = candidate;
  generation_ = generation;
  return true;
}

void SceneCatalog::clear_users(uint32_t generation) {
  scene_bank_clear(users_);
  generation_ = generation;
}

uint8_t SceneCatalog::show_eligible_ids(uint8_t *out, size_t capacity) const {
  if (out == nullptr || capacity == 0U) return 0;
  uint8_t count = 0;
  for (uint8_t id = 1; id <= SCENE_BUILTIN_COUNT && count < capacity; ++id) {
    if (BUILTINS[id - 1U].show_eligible) out[count++] = id;
  }
  for (uint8_t slot = 1; slot <= SCENE_USER_SLOT_COUNT && count < capacity;
       ++slot) {
    const SceneV1 *scene = user_at(slot);
    if (scene != nullptr && scene->show_eligible) out[count++] = scene->scene_id;
  }
  return count;
}

SceneOrigin scene_origin(uint8_t scene_id) {
  if (scene_id_is_builtin(scene_id)) return SceneOrigin::Builtin;
  if (scene_id_is_user(scene_id)) return SceneOrigin::User;
  return SceneOrigin::None;
}

const char *scene_origin_name(SceneOrigin origin) {
  switch (origin) {
    case SceneOrigin::Builtin: return "builtin";
    case SceneOrigin::User: return "user";
    case SceneOrigin::None: return "none";
  }
  return "none";
}

const char *scene_key(uint8_t scene_id) {
  if (scene_id_is_builtin(scene_id)) return BUILTIN_KEYS[scene_id - 1U];
  const uint8_t slot = scene_slot_from_id(scene_id);
  return slot == 0U ? "none" : USER_KEYS[slot - 1U];
}

} // namespace led
