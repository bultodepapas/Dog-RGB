#pragma once

#include <stddef.h>
#include <stdint.h>

#include "led/led_color.h"

namespace led {

static const uint8_t SCENE_SCHEMA_VERSION = 1;
static const uint8_t SCENE_REGISTRY_VERSION = 1;
static const uint8_t SCENE_BUILTIN_COUNT = 4;
static const uint8_t SCENE_USER_SLOT_COUNT = 4;
static const uint8_t SCENE_CATALOG_CAPACITY =
    SCENE_BUILTIN_COUNT + SCENE_USER_SLOT_COUNT;
static const uint8_t SCENE_ID_NONE = 0;
static const uint8_t SCENE_ID_USER_FIRST = 128;
static const uint8_t SCENE_ID_INVALID = 255;
static const size_t SCENE_NAME_BYTES = 24;
static const size_t SCENE_WIRE_BYTES = 44;
static const uint16_t SCENE_TRANSITION_MAX_MS = 5000;

struct SceneV1 {
  uint8_t scene_id;
  bool mirror;
  bool show_eligible;
  uint8_t effect_a;
  uint8_t effect_b;
  uint8_t palette_a;
  uint8_t palette_b;
  uint8_t speed;
  uint8_t intensity;
  uint8_t body_level;
  uint16_t transition_ms;
  Rgb base;
  Rgb accent;
  char name[SCENE_NAME_BYTES];
};

struct SceneBank {
  SceneV1 slots[SCENE_USER_SLOT_COUNT];
  uint8_t occupied_mask;
};

enum class SceneValidationError : uint8_t {
  None = 0,
  InvalidWire,
  InvalidId,
  InvalidName,
  InvalidUtf8,
  InvalidBodyLevel,
  InvalidTransition,
  InvalidEffectA,
  InvalidEffectB,
  InvalidPaletteA,
  InvalidPaletteB,
  MirrorMismatch,
  AdvancedShow,
};

bool scene_id_is_builtin(uint8_t scene_id);
bool scene_id_is_user(uint8_t scene_id);
uint8_t scene_id_from_slot(uint8_t slot_one_based);
uint8_t scene_slot_from_id(uint8_t scene_id);

bool scene_set_name(SceneV1 &scene, const char *name, size_t length);
bool scene_validate(const SceneV1 &scene,
                    SceneValidationError *error = nullptr);
const char *scene_validation_error_name(SceneValidationError error);

bool scene_encode(const SceneV1 &scene, uint8_t *out, size_t out_size,
                  SceneValidationError *error = nullptr);
bool scene_decode(const uint8_t *data, size_t data_size, SceneV1 &out,
                  SceneValidationError *error = nullptr);
bool scene_semantic_equal(const SceneV1 &left, const SceneV1 &right);
uint32_t scene_fingerprint(const SceneV1 &scene);

bool scene_bank_equal(const SceneBank &left, const SceneBank &right);
void scene_bank_clear(SceneBank &bank);

} // namespace led
