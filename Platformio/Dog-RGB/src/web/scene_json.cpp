#include "web/scene_json.h"

#include <stdio.h>
#include <string.h>

#include "led/effect_registry.h"
#include "led/palette_registry.h"

namespace scene_json {
namespace {

static void clear_error(Error &error) {
  error.code = ErrorCode::None;
  error.validation_error = led::SceneValidationError::None;
  memset(error.field, 0, sizeof(error.field));
}

static bool fail(Error &error, ErrorCode code, const char *field,
                 led::SceneValidationError validation =
                     led::SceneValidationError::None) {
  error.code = code;
  error.validation_error = validation;
  snprintf(error.field, sizeof(error.field), "%s",
           field == nullptr ? "" : field);
  return false;
}

static bool key_allowed(const char *key, const char *const *allowed,
                        size_t allowed_count) {
  for (size_t i = 0; i < allowed_count; ++i) {
    if (strcmp(key, allowed[i]) == 0) return true;
  }
  return false;
}

static bool has_key(JsonObjectConst object, const char *key) {
  for (JsonPairConst pair : object) {
    if (strcmp(pair.key().c_str(), key) == 0) return true;
  }
  return false;
}

static bool reject_unknown(JsonObjectConst object,
                           const char *const *allowed,
                           size_t allowed_count, Error &error,
                           const char *prefix) {
  for (JsonPairConst pair : object) {
    if (key_allowed(pair.key().c_str(), allowed, allowed_count)) continue;
    char path[64] = {};
    snprintf(path, sizeof(path), "%s%s%s", prefix == nullptr ? "" : prefix,
             prefix != nullptr && prefix[0] != '\0' ? "." : "",
             pair.key().c_str());
    return fail(error, ErrorCode::UnknownField, path);
  }
  return true;
}

static bool require_fields(JsonObjectConst object,
                           const char *const *required,
                           size_t required_count, Error &error,
                           const char *prefix) {
  for (size_t i = 0; i < required_count; ++i) {
    if (has_key(object, required[i])) continue;
    char path[64] = {};
    snprintf(path, sizeof(path), "%s%s%s", prefix == nullptr ? "" : prefix,
             prefix != nullptr && prefix[0] != '\0' ? "." : "",
             required[i]);
    return fail(error, ErrorCode::MissingField, path);
  }
  return true;
}

static bool parse_root(const char *data, size_t size, JsonDocument &document,
                       JsonObjectConst &root, Error &error) {
  clear_error(error);
  if (data == nullptr || size < SCENE_JSON_BODY_MIN_BYTES ||
      size > SCENE_JSON_BODY_MAX_BYTES) {
    return fail(error, ErrorCode::InvalidJson, "body");
  }
  const DeserializationError parsed = deserializeJson(
      document, data, size,
      DeserializationOption::NestingLimit(SCENE_JSON_NESTING_LIMIT));
  if (parsed) {
    return fail(error,
                parsed == DeserializationError::TooDeep ? ErrorCode::TooDeep
                                                        : ErrorCode::InvalidJson,
                "body");
  }
  if (!document.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::ExpectedObject, "body");
  }
  root = document.as<JsonObjectConst>();
  return true;
}

static bool read_u32(JsonObjectConst object, const char *key, uint32_t &out,
                     Error &error, const char *path) {
  const JsonVariantConst value = object[key];
  if (!value.is<uint32_t>() || value.is<bool>()) {
    return fail(error, ErrorCode::InvalidType, path);
  }
  out = value.as<uint32_t>();
  return true;
}

static bool read_u8(JsonObjectConst object, const char *key, uint8_t &out,
                    Error &error, const char *path) {
  uint32_t value = 0;
  if (!read_u32(object, key, value, error, path)) return false;
  if (value > UINT8_MAX) return fail(error, ErrorCode::InvalidValue, path);
  out = static_cast<uint8_t>(value);
  return true;
}

static bool read_u16(JsonObjectConst object, const char *key, uint16_t &out,
                     Error &error, const char *path) {
  uint32_t value = 0;
  if (!read_u32(object, key, value, error, path)) return false;
  if (value > UINT16_MAX) return fail(error, ErrorCode::InvalidValue, path);
  out = static_cast<uint16_t>(value);
  return true;
}

static bool read_bool(JsonObjectConst object, const char *key, bool &out,
                      Error &error, const char *path) {
  const JsonVariantConst value = object[key];
  if (!value.is<bool>()) return fail(error, ErrorCode::InvalidType, path);
  out = value.as<bool>();
  return true;
}

static bool json_string_equals(JsonVariantConst value, const char *expected) {
  if (!value.is<JsonString>() || expected == nullptr) return false;
  const JsonString actual = value.as<JsonString>();
  const size_t expected_size = strlen(expected);
  return actual.size() == expected_size &&
         memcmp(actual.c_str(), expected, expected_size) == 0;
}

static bool read_rgb(JsonObjectConst parent, const char *key, led::Rgb &out,
                     Error &error, const char *path) {
  const JsonVariantConst variant = parent[key];
  if (!variant.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, path);
  }
  const JsonObjectConst object = variant.as<JsonObjectConst>();
  static const char *const FIELDS[] = {"r", "g", "b"};
  if (!reject_unknown(object, FIELDS, 3, error, path) ||
      !require_fields(object, FIELDS, 3, error, path)) {
    return false;
  }
  char component[64] = {};
  snprintf(component, sizeof(component), "%s.r", path);
  if (!read_u8(object, "r", out.r, error, component)) return false;
  snprintf(component, sizeof(component), "%s.g", path);
  if (!read_u8(object, "g", out.g, error, component)) return false;
  snprintf(component, sizeof(component), "%s.b", path);
  return read_u8(object, "b", out.b, error, component);
}

static const char *palette_key(uint8_t palette_id) {
  if (palette_id == led::PALETTE_NONE) return "none";
  const led::PaletteDescriptor *descriptor =
      led::palette_descriptor(palette_id);
  return descriptor == nullptr ? "invalid" : descriptor->key;
}

static bool read_effect_reference(
    JsonObjectConst branch, uint8_t &effect_id, Error &error,
    const char *path, led::SceneValidationError invalid_effect) {
  const JsonVariantConst variant = branch["effect"];
  if (!variant.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, path);
  }
  const JsonObjectConst object = variant.as<JsonObjectConst>();
  static const char *const FIELDS[] = {"id", "key"};
  if (!reject_unknown(object, FIELDS, 2, error, path) ||
      !require_fields(object, FIELDS, 2, error, path)) {
    return false;
  }
  char id_path[64] = {};
  snprintf(id_path, sizeof(id_path), "%s.id", path);
  if (!read_u8(object, "id", effect_id, error, id_path)) return false;
  const led::EffectDescriptor *descriptor =
      led::effect_descriptor(effect_id);
  if (descriptor == nullptr) {
    return fail(error, ErrorCode::InvalidScene, id_path, invalid_effect);
  }
  char key_path[64] = {};
  snprintf(key_path, sizeof(key_path), "%s.key", path);
  if (!json_string_equals(object["key"], descriptor->key)) {
    return fail(error, ErrorCode::ReferenceMismatch, key_path);
  }
  return true;
}

static bool read_palette_reference(
    JsonObjectConst branch, uint8_t &palette_id, Error &error,
    const char *path, led::SceneValidationError invalid_palette) {
  const JsonVariantConst variant = branch["palette"];
  if (!variant.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, path);
  }
  const JsonObjectConst object = variant.as<JsonObjectConst>();
  static const char *const FIELDS[] = {"id", "key"};
  if (!reject_unknown(object, FIELDS, 2, error, path) ||
      !require_fields(object, FIELDS, 2, error, path)) {
    return false;
  }
  char id_path[64] = {};
  snprintf(id_path, sizeof(id_path), "%s.id", path);
  uint32_t raw_id = 0;
  if (!read_u32(object, "id", raw_id, error, id_path) || raw_id > UINT8_MAX) {
    if (error.code == ErrorCode::None) {
      return fail(error, ErrorCode::InvalidValue, id_path);
    }
    return false;
  }
  palette_id = static_cast<uint8_t>(raw_id);
  if (palette_id != led::PALETTE_NONE &&
      !led::palette_id_valid(palette_id)) {
    return fail(error, ErrorCode::InvalidScene, id_path, invalid_palette);
  }
  char key_path[64] = {};
  snprintf(key_path, sizeof(key_path), "%s.key", path);
  if (!json_string_equals(object["key"], palette_key(palette_id))) {
    return fail(error, ErrorCode::ReferenceMismatch, key_path);
  }
  return true;
}

static bool read_branch(JsonObjectConst scene, const char *key,
                        uint8_t &effect_id, uint8_t &palette_id,
                        Error &error, const char *path,
                        led::SceneValidationError invalid_effect,
                        led::SceneValidationError invalid_palette) {
  const JsonVariantConst variant = scene[key];
  if (!variant.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, path);
  }
  const JsonObjectConst object = variant.as<JsonObjectConst>();
  static const char *const FIELDS[] = {"effect", "palette"};
  if (!reject_unknown(object, FIELDS, 2, error, path) ||
      !require_fields(object, FIELDS, 2, error, path)) {
    return false;
  }
  char effect_path[64] = {};
  char palette_path[64] = {};
  snprintf(effect_path, sizeof(effect_path), "%s.effect", path);
  snprintf(palette_path, sizeof(palette_path), "%s.palette", path);
  return read_effect_reference(object, effect_id, error, effect_path,
                               invalid_effect) &&
         read_palette_reference(object, palette_id, error, palette_path,
                                invalid_palette);
}

static bool read_scene(JsonObjectConst object, bool with_identity,
                       uint8_t expected_slot, led::SceneV1 &out,
                       Error &error, const char *path) {
  static const char *const FIELDS[] = {
      "id",          "slot",          "name",       "mirror",
      "show_eligible", "speed",       "intensity",  "body_level",
      "transition_ms", "base_rgb",    "accent_rgb", "branch_a",
      "branch_b"};
  static const char *const SAVE_FIELDS[] = {
      "name",       "mirror",        "show_eligible", "speed",
      "intensity",  "body_level",    "transition_ms", "base_rgb",
      "accent_rgb", "branch_a",      "branch_b"};
  const char *const *allowed = with_identity ? FIELDS : SAVE_FIELDS;
  const size_t allowed_count = with_identity ? 13U : 11U;
  if (!reject_unknown(object, allowed, allowed_count, error, path) ||
      !require_fields(object, allowed, allowed_count, error, path)) {
    return false;
  }

  led::SceneV1 scene{};
  uint8_t slot = expected_slot;
  if (with_identity) {
    char slot_path[64] = {};
    char id_path[64] = {};
    snprintf(slot_path, sizeof(slot_path), "%s.slot", path);
    snprintf(id_path, sizeof(id_path), "%s.id", path);
    uint8_t scene_id = 0;
    if (!read_u8(object, "slot", slot, error, slot_path) ||
        !read_u8(object, "id", scene_id, error, id_path)) {
      return false;
    }
    if (slot < 1U || slot > led::SCENE_USER_SLOT_COUNT ||
        scene_id != led::scene_id_from_slot(slot)) {
      return fail(error, ErrorCode::ReferenceMismatch, id_path);
    }
    scene.scene_id = scene_id;
  } else {
    scene.scene_id = led::scene_id_from_slot(slot);
  }

  const JsonVariantConst name_variant = object["name"];
  if (!name_variant.is<JsonString>()) {
    char name_path[64] = {};
    snprintf(name_path, sizeof(name_path), "%s.name", path);
    return fail(error, ErrorCode::InvalidType, name_path);
  }
  const JsonString name = name_variant.as<JsonString>();
  if (!led::scene_set_name(scene, name.c_str(), name.size())) {
    char name_path[64] = {};
    snprintf(name_path, sizeof(name_path), "%s.name", path);
    return fail(error, ErrorCode::InvalidScene, name_path,
                led::SceneValidationError::InvalidName);
  }

  char field[64] = {};
  snprintf(field, sizeof(field), "%s.mirror", path);
  if (!read_bool(object, "mirror", scene.mirror, error, field)) return false;
  snprintf(field, sizeof(field), "%s.show_eligible", path);
  if (!read_bool(object, "show_eligible", scene.show_eligible, error, field)) {
    return false;
  }
  snprintf(field, sizeof(field), "%s.speed", path);
  if (!read_u8(object, "speed", scene.speed, error, field)) return false;
  snprintf(field, sizeof(field), "%s.intensity", path);
  if (!read_u8(object, "intensity", scene.intensity, error, field)) {
    return false;
  }
  snprintf(field, sizeof(field), "%s.body_level", path);
  if (!read_u8(object, "body_level", scene.body_level, error, field)) {
    return false;
  }
  snprintf(field, sizeof(field), "%s.transition_ms", path);
  if (!read_u16(object, "transition_ms", scene.transition_ms, error, field)) {
    return false;
  }
  snprintf(field, sizeof(field), "%s.base_rgb", path);
  if (!read_rgb(object, "base_rgb", scene.base, error, field)) return false;
  snprintf(field, sizeof(field), "%s.accent_rgb", path);
  if (!read_rgb(object, "accent_rgb", scene.accent, error, field)) return false;
  snprintf(field, sizeof(field), "%s.branch_a", path);
  if (!read_branch(object, "branch_a", scene.effect_a, scene.palette_a,
                   error, field, led::SceneValidationError::InvalidEffectA,
                   led::SceneValidationError::InvalidPaletteA)) {
    return false;
  }
  snprintf(field, sizeof(field), "%s.branch_b", path);
  if (!read_branch(object, "branch_b", scene.effect_b, scene.palette_b,
                   error, field, led::SceneValidationError::InvalidEffectB,
                   led::SceneValidationError::InvalidPaletteB)) {
    return false;
  }

  led::SceneValidationError validation = led::SceneValidationError::None;
  if (!led::scene_validate(scene, &validation)) {
    return fail(error, ErrorCode::InvalidScene, path, validation);
  }
  out = scene;
  return true;
}

static void append_rgb(JsonObject parent, const char *key,
                       const led::Rgb &color) {
  JsonObject out = parent[key].to<JsonObject>();
  out["r"] = color.r;
  out["g"] = color.g;
  out["b"] = color.b;
}

static void append_branch(JsonObject parent, const char *key,
                          uint8_t effect_id, uint8_t palette_id) {
  JsonObject branch = parent[key].to<JsonObject>();
  JsonObject effect = branch["effect"].to<JsonObject>();
  const led::EffectDescriptor *effect_descriptor =
      led::effect_descriptor(effect_id);
  effect["id"] = effect_id;
  effect["key"] = effect_descriptor == nullptr ? "invalid"
                                                   : effect_descriptor->key;
  JsonObject palette = branch["palette"].to<JsonObject>();
  palette["id"] = palette_id;
  palette["key"] = palette_key(palette_id);
}

} // namespace

bool parse_apply(const char *data, size_t size, ApplyRequest &out,
                 Error &error) {
  JsonDocument document;
  JsonObjectConst root;
  if (!parse_root(data, size, document, root, error)) return false;
  static const char *const FIELDS[] = {"id"};
  if (!reject_unknown(root, FIELDS, 1, error, "") ||
      !require_fields(root, FIELDS, 1, error, "")) {
    return false;
  }
  if (!read_u8(root, "id", out.scene_id, error, "id")) return false;
  if (!led::scene_id_is_builtin(out.scene_id) &&
      !led::scene_id_is_user(out.scene_id)) {
    return fail(error, ErrorCode::InvalidValue, "id");
  }
  return true;
}

bool parse_cancel(const char *data, size_t size, Error &error) {
  JsonDocument document;
  JsonObjectConst root;
  if (!parse_root(data, size, document, root, error)) return false;
  static const char *const NO_FIELDS[] = {nullptr};
  return reject_unknown(root, NO_FIELDS, 0, error, "");
}

bool parse_save(const char *data, size_t size, SaveRequest &out,
                Error &error) {
  JsonDocument document;
  JsonObjectConst root;
  if (!parse_root(data, size, document, root, error)) return false;
  static const char *const FIELDS[] = {"expected_generation", "slot", "scene"};
  if (!reject_unknown(root, FIELDS, 3, error, "") ||
      !require_fields(root, FIELDS, 3, error, "")) {
    return false;
  }
  if (!read_u32(root, "expected_generation", out.expected_generation, error,
                "expected_generation") ||
      !read_u8(root, "slot", out.slot, error, "slot")) {
    return false;
  }
  if (out.slot < 1U || out.slot > led::SCENE_USER_SLOT_COUNT) {
    return fail(error, ErrorCode::InvalidValue, "slot");
  }
  const JsonVariantConst scene = root["scene"];
  if (!scene.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, "scene");
  }
  return read_scene(scene.as<JsonObjectConst>(), false, out.slot, out.scene,
                    error, "scene");
}

bool parse_delete(const char *data, size_t size, DeleteRequest &out,
                  Error &error) {
  JsonDocument document;
  JsonObjectConst root;
  if (!parse_root(data, size, document, root, error)) return false;
  static const char *const FIELDS[] = {"expected_generation", "slot"};
  if (!reject_unknown(root, FIELDS, 2, error, "") ||
      !require_fields(root, FIELDS, 2, error, "")) {
    return false;
  }
  if (!read_u32(root, "expected_generation", out.expected_generation, error,
                "expected_generation") ||
      !read_u8(root, "slot", out.slot, error, "slot")) {
    return false;
  }
  return out.slot >= 1U && out.slot <= led::SCENE_USER_SLOT_COUNT
             ? true
             : fail(error, ErrorCode::InvalidValue, "slot");
}

bool parse_import(const char *data, size_t size, ImportRequest &out,
                  Error &error) {
  memset(&out, 0, sizeof(out));
  led::scene_bank_clear(out.bank);
  JsonDocument parsed;
  JsonObjectConst root;
  if (!parse_root(data, size, parsed, root, error)) return false;
  static const char *const ROOT_FIELDS[] = {
      "expected_generation", "dry_run", "recover_corrupt", "document"};
  static const char *const ROOT_REQUIRED[] = {
      "dry_run", "recover_corrupt", "document"};
  if (!reject_unknown(root, ROOT_FIELDS, 4, error, "") ||
      !require_fields(root, ROOT_REQUIRED, 3, error, "")) {
    return false;
  }
  if (!read_bool(root, "dry_run", out.dry_run, error, "dry_run") ||
      !read_bool(root, "recover_corrupt", out.recover_corrupt, error,
                 "recover_corrupt")) {
    return false;
  }
  out.has_expected_generation = has_key(root, "expected_generation");
  if (out.has_expected_generation &&
      !read_u32(root, "expected_generation", out.expected_generation, error,
                "expected_generation")) {
    return false;
  }
  if (!out.dry_run && !out.has_expected_generation) {
    return fail(error, ErrorCode::MissingField, "expected_generation");
  }
  const JsonVariantConst document_variant = root["document"];
  if (!document_variant.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, "document");
  }
  const JsonObjectConst document = document_variant.as<JsonObjectConst>();
  static const char *const DOCUMENT_FIELDS[] = {
      "format", "schema_version", "store_generation", "registry", "scenes"};
  if (!reject_unknown(document, DOCUMENT_FIELDS, 5, error, "document") ||
      !require_fields(document, DOCUMENT_FIELDS, 5, error, "document")) {
    return false;
  }
  if (!json_string_equals(document["format"], "dog-rgb-scenes")) {
    return fail(error, ErrorCode::UnsupportedSchema, "document.format");
  }
  uint32_t schema_version = 0;
  uint32_t ignored_generation = 0;
  if (!read_u32(document, "schema_version", schema_version, error,
                "document.schema_version") ||
      !read_u32(document, "store_generation", ignored_generation, error,
                "document.store_generation")) {
    return false;
  }
  (void)ignored_generation;
  if (schema_version != led::SCENE_SCHEMA_VERSION) {
    return fail(error, ErrorCode::UnsupportedSchema,
                "document.schema_version");
  }

  const JsonVariantConst registry_variant = document["registry"];
  if (!registry_variant.is<JsonObjectConst>()) {
    return fail(error, ErrorCode::InvalidType, "document.registry");
  }
  const JsonObjectConst registry = registry_variant.as<JsonObjectConst>();
  static const char *const REGISTRY_FIELDS[] = {"effects", "palettes"};
  if (!reject_unknown(registry, REGISTRY_FIELDS, 2, error,
                      "document.registry") ||
      !require_fields(registry, REGISTRY_FIELDS, 2, error,
                      "document.registry")) {
    return false;
  }
  uint32_t effects_version = 0;
  uint32_t palettes_version = 0;
  if (!read_u32(registry, "effects", effects_version, error,
                "document.registry.effects") ||
      !read_u32(registry, "palettes", palettes_version, error,
                "document.registry.palettes")) {
    return false;
  }
  out.effects_registry_mismatch =
      effects_version != led::EFFECT_REGISTRY_VERSION;
  out.palettes_registry_mismatch =
      palettes_version != led::PALETTE_REGISTRY_VERSION;

  const JsonVariantConst scenes_variant = document["scenes"];
  if (!scenes_variant.is<JsonArrayConst>()) {
    return fail(error, ErrorCode::InvalidType, "document.scenes");
  }
  const JsonArrayConst scenes = scenes_variant.as<JsonArrayConst>();
  if (scenes.size() > led::SCENE_USER_SLOT_COUNT) {
    return fail(error, ErrorCode::InvalidValue, "document.scenes");
  }
  uint8_t index = 0;
  for (JsonVariantConst value : scenes) {
    char path[64] = {};
    snprintf(path, sizeof(path), "document.scenes[%u]", index++);
    if (!value.is<JsonObjectConst>()) {
      return fail(error, ErrorCode::InvalidType, path);
    }
    led::SceneV1 scene{};
    if (!read_scene(value.as<JsonObjectConst>(), true, 0, scene, error,
                    path)) {
      return false;
    }
    const uint8_t slot = led::scene_slot_from_id(scene.scene_id);
    const uint8_t bit = static_cast<uint8_t>(1U << (slot - 1U));
    if ((out.bank.occupied_mask & bit) != 0U) {
      return fail(error, ErrorCode::InvalidValue, path);
    }
    out.bank.slots[slot - 1U] = scene;
    out.bank.occupied_mask |= bit;
    out.scene_count++;
  }
  return true;
}

void append_scene(JsonObject out, const led::SceneV1 &scene,
                  bool include_identity, uint8_t slot) {
  if (include_identity) {
    out["id"] = scene.scene_id;
    out["slot"] = slot == 0U ? led::scene_slot_from_id(scene.scene_id) : slot;
  }
  out["name"] = scene.name;
  out["mirror"] = scene.mirror;
  out["show_eligible"] = scene.show_eligible;
  out["speed"] = scene.speed;
  out["intensity"] = scene.intensity;
  out["body_level"] = scene.body_level;
  out["transition_ms"] = scene.transition_ms;
  append_rgb(out, "base_rgb", scene.base);
  append_rgb(out, "accent_rgb", scene.accent);
  append_branch(out, "branch_a", scene.effect_a, scene.palette_a);
  append_branch(out, "branch_b", scene.effect_b, scene.palette_b);
}

void build_export(JsonDocument &document, const led::SceneBank &bank,
                  uint32_t store_generation) {
  document.clear();
  document["format"] = "dog-rgb-scenes";
  document["schema_version"] = led::SCENE_SCHEMA_VERSION;
  document["store_generation"] = store_generation;
  JsonObject registry = document["registry"].to<JsonObject>();
  registry["effects"] = led::EFFECT_REGISTRY_VERSION;
  registry["palettes"] = led::PALETTE_REGISTRY_VERSION;
  JsonArray scenes = document["scenes"].to<JsonArray>();
  for (uint8_t slot = 1; slot <= led::SCENE_USER_SLOT_COUNT; ++slot) {
    const uint8_t bit = static_cast<uint8_t>(1U << (slot - 1U));
    if ((bank.occupied_mask & bit) == 0U) continue;
    JsonObject scene = scenes.add<JsonObject>();
    append_scene(scene, bank.slots[slot - 1U], true, slot);
  }
}

const char *error_name(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "none";
    case ErrorCode::InvalidJson: return "invalid_json";
    case ErrorCode::TooDeep: return "too_deep";
    case ErrorCode::ExpectedObject: return "expected_object";
    case ErrorCode::MissingField: return "missing_field";
    case ErrorCode::UnknownField: return "unknown_field";
    case ErrorCode::InvalidType: return "invalid_type";
    case ErrorCode::InvalidValue: return "invalid_value";
    case ErrorCode::InvalidScene: return "invalid_scene";
    case ErrorCode::ReferenceMismatch: return "reference_mismatch";
    case ErrorCode::UnsupportedSchema: return "unsupported_schema";
  }
  return "unknown";
}

} // namespace scene_json
