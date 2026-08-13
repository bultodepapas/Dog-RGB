#include "led/scene.h"

#include <string.h>

#include "led/effect_registry.h"
#include "led/palette_registry.h"
#include "util/crc32.h"

namespace led {
namespace {

static void set_error(SceneValidationError *out,
                      SceneValidationError value) {
  if (out != nullptr) *out = value;
}

static size_t canonical_name_length(const char *name, bool &canonical) {
  canonical = false;
  size_t length = 0;
  while (length < SCENE_NAME_BYTES && name[length] != '\0') ++length;
  if (length == 0 || length >= SCENE_NAME_BYTES) return length;
  for (size_t i = length + 1; i < SCENE_NAME_BYTES; ++i) {
    if (name[i] != '\0') return length;
  }
  canonical = true;
  return length;
}

static bool utf8_name_valid(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    const uint8_t first = bytes[i++];
    uint32_t codepoint = 0;
    uint8_t continuation_count = 0;
    uint32_t minimum = 0;
    if (first < 0x80U) {
      codepoint = first;
    } else if ((first & 0xE0U) == 0xC0U) {
      codepoint = first & 0x1FU;
      continuation_count = 1;
      minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
      codepoint = first & 0x0FU;
      continuation_count = 2;
      minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
      codepoint = first & 0x07U;
      continuation_count = 3;
      minimum = 0x10000U;
    } else {
      return false;
    }
    if (i + continuation_count > length) return false;
    for (uint8_t part = 0; part < continuation_count; ++part) {
      const uint8_t next = bytes[i++];
      if ((next & 0xC0U) != 0x80U) return false;
      codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    if (codepoint < minimum || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
      return false;
    }
    if (codepoint < 0x20U || codepoint == 0x7FU ||
        (codepoint >= 0x80U && codepoint <= 0x9FU)) {
      return false;
    }
  }
  return true;
}

static bool palette_matches_effect(uint8_t effect_id, uint8_t palette_id) {
  const EffectDescriptor *effect = effect_descriptor(effect_id);
  if (effect == nullptr) return false;
  switch (effect->palette_mode) {
    case EffectPaletteMode::None:
      return palette_id == PALETTE_NONE;
    case EffectPaletteMode::Internal:
      return palette_id == effect->default_palette_id &&
             palette_id_valid(palette_id);
    case EffectPaletteMode::Selectable:
      return palette_id_valid(palette_id);
  }
  return false;
}

static bool advanced_effect(uint8_t effect_id) {
  const EffectDescriptor *effect = effect_descriptor(effect_id);
  return effect != nullptr && effect->safety == EffectSafetyClass::Advanced;
}

static void write_u16_le(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

static uint16_t read_u16_le(const uint8_t *in) {
  return static_cast<uint16_t>(in[0]) |
         (static_cast<uint16_t>(in[1]) << 8U);
}

} // namespace

bool scene_id_is_builtin(uint8_t scene_id) {
  return scene_id >= 1U && scene_id <= SCENE_BUILTIN_COUNT;
}

bool scene_id_is_user(uint8_t scene_id) {
  return scene_id >= SCENE_ID_USER_FIRST &&
         scene_id < SCENE_ID_USER_FIRST + SCENE_USER_SLOT_COUNT;
}

uint8_t scene_id_from_slot(uint8_t slot_one_based) {
  if (slot_one_based < 1U || slot_one_based > SCENE_USER_SLOT_COUNT) {
    return SCENE_ID_INVALID;
  }
  return static_cast<uint8_t>(SCENE_ID_USER_FIRST + slot_one_based - 1U);
}

uint8_t scene_slot_from_id(uint8_t scene_id) {
  return scene_id_is_user(scene_id)
             ? static_cast<uint8_t>(scene_id - SCENE_ID_USER_FIRST + 1U)
             : 0U;
}

bool scene_set_name(SceneV1 &scene, const char *name, size_t length) {
  if (name == nullptr || length == 0 || length >= SCENE_NAME_BYTES) {
    return false;
  }
  if (!utf8_name_valid(reinterpret_cast<const uint8_t *>(name), length)) {
    return false;
  }
  memset(scene.name, 0, sizeof(scene.name));
  memcpy(scene.name, name, length);
  return true;
}

bool scene_validate(const SceneV1 &scene, SceneValidationError *error) {
  set_error(error, SceneValidationError::None);
  if (!scene_id_is_builtin(scene.scene_id) &&
      !scene_id_is_user(scene.scene_id)) {
    set_error(error, SceneValidationError::InvalidId);
    return false;
  }
  bool canonical_name = false;
  const size_t name_length = canonical_name_length(scene.name, canonical_name);
  if (!canonical_name) {
    set_error(error, SceneValidationError::InvalidName);
    return false;
  }
  if (!utf8_name_valid(reinterpret_cast<const uint8_t *>(scene.name),
                       name_length)) {
    set_error(error, SceneValidationError::InvalidUtf8);
    return false;
  }
  if (scene.body_level == 0U) {
    set_error(error, SceneValidationError::InvalidBodyLevel);
    return false;
  }
  if (scene.transition_ms > SCENE_TRANSITION_MAX_MS) {
    set_error(error, SceneValidationError::InvalidTransition);
    return false;
  }
  if (!effect_id_valid(scene.effect_a)) {
    set_error(error, SceneValidationError::InvalidEffectA);
    return false;
  }
  if (!effect_id_valid(scene.effect_b)) {
    set_error(error, SceneValidationError::InvalidEffectB);
    return false;
  }
  if (!palette_matches_effect(scene.effect_a, scene.palette_a)) {
    set_error(error, SceneValidationError::InvalidPaletteA);
    return false;
  }
  if (!palette_matches_effect(scene.effect_b, scene.palette_b)) {
    set_error(error, SceneValidationError::InvalidPaletteB);
    return false;
  }
  if (scene.mirror &&
      (scene.effect_a != scene.effect_b ||
       scene.palette_a != scene.palette_b)) {
    set_error(error, SceneValidationError::MirrorMismatch);
    return false;
  }
  if (scene.show_eligible &&
      (advanced_effect(scene.effect_a) || advanced_effect(scene.effect_b))) {
    set_error(error, SceneValidationError::AdvancedShow);
    return false;
  }
  return true;
}

const char *scene_validation_error_name(SceneValidationError error) {
  switch (error) {
    case SceneValidationError::None: return "none";
    case SceneValidationError::InvalidWire: return "invalid_wire";
    case SceneValidationError::InvalidId: return "invalid_id";
    case SceneValidationError::InvalidName: return "invalid_name";
    case SceneValidationError::InvalidUtf8: return "invalid_utf8";
    case SceneValidationError::InvalidBodyLevel: return "invalid_body_level";
    case SceneValidationError::InvalidTransition: return "invalid_transition";
    case SceneValidationError::InvalidEffectA: return "invalid_effect_a";
    case SceneValidationError::InvalidEffectB: return "invalid_effect_b";
    case SceneValidationError::InvalidPaletteA: return "invalid_palette_a";
    case SceneValidationError::InvalidPaletteB: return "invalid_palette_b";
    case SceneValidationError::MirrorMismatch: return "mirror_mismatch";
    case SceneValidationError::AdvancedShow: return "advanced_show";
  }
  return "unknown";
}

bool scene_encode(const SceneV1 &scene, uint8_t *out, size_t out_size,
                  SceneValidationError *error) {
  if (out == nullptr || out_size != SCENE_WIRE_BYTES) {
    set_error(error, SceneValidationError::InvalidWire);
    return false;
  }
  if (!scene_validate(scene, error)) return false;
  memset(out, 0, out_size);
  out[0] = scene.scene_id;
  out[1] = static_cast<uint8_t>((scene.mirror ? 0x01U : 0U) |
                                (scene.show_eligible ? 0x02U : 0U));
  out[2] = scene.effect_a;
  out[3] = scene.effect_b;
  out[4] = scene.palette_a;
  out[5] = scene.palette_b;
  out[6] = scene.speed;
  out[7] = scene.intensity;
  out[8] = scene.body_level;
  write_u16_le(out + 9, scene.transition_ms);
  out[11] = scene.base.r;
  out[12] = scene.base.g;
  out[13] = scene.base.b;
  out[14] = scene.accent.r;
  out[15] = scene.accent.g;
  out[16] = scene.accent.b;
  memcpy(out + 17, scene.name, SCENE_NAME_BYTES);
  return true;
}

bool scene_decode(const uint8_t *data, size_t data_size, SceneV1 &out,
                  SceneValidationError *error) {
  set_error(error, SceneValidationError::None);
  if (data == nullptr || data_size != SCENE_WIRE_BYTES ||
      (data[1] & 0xFCU) != 0U || data[41] != 0U || data[42] != 0U ||
      data[43] != 0U) {
    set_error(error, SceneValidationError::InvalidWire);
    return false;
  }
  SceneV1 candidate = {};
  candidate.scene_id = data[0];
  candidate.mirror = (data[1] & 0x01U) != 0U;
  candidate.show_eligible = (data[1] & 0x02U) != 0U;
  candidate.effect_a = data[2];
  candidate.effect_b = data[3];
  candidate.palette_a = data[4];
  candidate.palette_b = data[5];
  candidate.speed = data[6];
  candidate.intensity = data[7];
  candidate.body_level = data[8];
  candidate.transition_ms = read_u16_le(data + 9);
  candidate.base = Rgb{data[11], data[12], data[13]};
  candidate.accent = Rgb{data[14], data[15], data[16]};
  memcpy(candidate.name, data + 17, SCENE_NAME_BYTES);
  if (!scene_validate(candidate, error)) return false;
  out = candidate;
  return true;
}

bool scene_semantic_equal(const SceneV1 &left, const SceneV1 &right) {
  uint8_t left_wire[SCENE_WIRE_BYTES] = {};
  uint8_t right_wire[SCENE_WIRE_BYTES] = {};
  return scene_encode(left, left_wire, sizeof(left_wire)) &&
         scene_encode(right, right_wire, sizeof(right_wire)) &&
         memcmp(left_wire, right_wire, sizeof(left_wire)) == 0;
}

uint32_t scene_fingerprint(const SceneV1 &scene) {
  uint8_t wire[SCENE_WIRE_BYTES] = {};
  return scene_encode(scene, wire, sizeof(wire))
             ? util::crc32_ieee(wire, sizeof(wire))
             : 0U;
}

bool scene_bank_equal(const SceneBank &left, const SceneBank &right) {
  if (left.occupied_mask != right.occupied_mask) return false;
  for (uint8_t i = 0; i < SCENE_USER_SLOT_COUNT; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1U << i);
    if ((left.occupied_mask & bit) != 0U &&
        !scene_semantic_equal(left.slots[i], right.slots[i])) {
      return false;
    }
  }
  return true;
}

void scene_bank_clear(SceneBank &bank) {
  memset(&bank, 0, sizeof(bank));
}

} // namespace led
