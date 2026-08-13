#include <ArduinoJson.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "led/scene_catalog.h"
#include "led/palette_registry.h"
#include "web/scene_json.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

static const char VALID_SAVE[] = R"json({
  "expected_generation":0,
  "slot":1,
  "scene":{
    "name":"Paseo azul",
    "mirror":true,
    "show_eligible":true,
    "speed":140,
    "intensity":170,
    "body_level":180,
    "transition_ms":600,
    "base_rgb":{"r":0,"g":40,"b":80},
    "accent_rgb":{"r":0,"g":180,"b":220},
    "branch_a":{"effect":{"id":4,"key":"comet"},"palette":{"id":2,"key":"ocean"}},
    "branch_b":{"effect":{"id":4,"key":"comet"},"palette":{"id":2,"key":"ocean"}}
  }
})json";

led::SceneBank sample_bank() {
  led::SceneCatalog catalog;
  led::SceneBank bank{};
  led::scene_bank_clear(bank);
  for (uint8_t index = 0; index < led::SCENE_USER_SLOT_COUNT; ++index) {
    bank.slots[index] = catalog.builtin_at(index);
    bank.slots[index].scene_id = led::scene_id_from_slot(index + 1U);
    char name[led::SCENE_NAME_BYTES] = {};
    const int name_size = std::snprintf(name, sizeof(name), "Exportada %u",
                                        static_cast<unsigned>(index + 1U));
    expect(name_size > 0 &&
               led::scene_set_name(bank.slots[index], name,
                                   static_cast<size_t>(name_size)),
           "export fixture name is invalid");
  }
  bank.occupied_mask = 0x0F;
  return bank;
}

void test_strict_mutation_parsers() {
  scene_json::Error error{};
  scene_json::ApplyRequest apply{};
  expect(scene_json::parse_apply("{\"id\":1}", 8, apply, error) &&
             apply.scene_id == 1,
         "valid apply command was rejected");
  expect(!scene_json::parse_apply("{\"id\":\"1\"}", 10, apply, error) &&
             error.code == scene_json::ErrorCode::InvalidType,
         "numeric string bypassed strict apply typing");
  expect(!scene_json::parse_apply("{\"id\":true}", 11, apply, error) &&
             error.code == scene_json::ErrorCode::InvalidType,
         "boolean bypassed strict integer typing");
  expect(!scene_json::parse_apply("{\"id\":1,\"x\":0}", 14, apply, error) &&
             error.code == scene_json::ErrorCode::UnknownField,
         "unknown apply field was ignored");
  expect(scene_json::parse_cancel("{}", 2, error),
         "empty cancel object was rejected");
  expect(!scene_json::parse_cancel("{\"id\":1}", 8, error) &&
             error.code == scene_json::ErrorCode::UnknownField,
         "cancel accepted an undocumented field");

  scene_json::SaveRequest save{};
  expect(scene_json::parse_save(VALID_SAVE, sizeof(VALID_SAVE) - 1U, save,
                                error) &&
             save.slot == 1 && save.expected_generation == 0 &&
             save.scene.scene_id == 128 && save.scene.effect_a == 4 &&
             save.scene.palette_a == led::PALETTE_OCEAN &&
             save.scene.body_level == 180,
         "canonical save payload did not decode to SceneV1");

  const char fractional[] = R"json({"expected_generation":0,"slot":1.5,"scene":{}})json";
  expect(!scene_json::parse_save(fractional, sizeof(fractional) - 1U, save,
                                 error) &&
             error.code == scene_json::ErrorCode::InvalidType,
         "fractional slot bypassed strict integer typing");
  const char unknown_scene[] = R"json({"expected_generation":0,"slot":1,"scene":{"secret":"x"}})json";
  expect(!scene_json::parse_save(unknown_scene, sizeof(unknown_scene) - 1U,
                                 save, error) &&
             error.code == scene_json::ErrorCode::UnknownField,
         "scene allowlist accepted a secret/unknown field");

  const char mismatch[] = R"json({"expected_generation":0,"slot":1,"scene":{"name":"X","mirror":true,"show_eligible":true,"speed":1,"intensity":1,"body_level":1,"transition_ms":0,"base_rgb":{"r":0,"g":0,"b":0},"accent_rgb":{"r":0,"g":0,"b":0},"branch_a":{"effect":{"id":4,"key":"wrong"},"palette":{"id":2,"key":"ocean"}},"branch_b":{"effect":{"id":4,"key":"comet"},"palette":{"id":2,"key":"ocean"}}}})json";
  expect(!scene_json::parse_save(mismatch, sizeof(mismatch) - 1U, save,
                                 error) &&
             error.code == scene_json::ErrorCode::ReferenceMismatch,
         "effect ID/key mismatch was accepted");

  JsonDocument invalid_branch_b;
  expect(!deserializeJson(invalid_branch_b, VALID_SAVE),
         "valid save fixture could not be prepared for branch-B diagnostics");
  invalid_branch_b["scene"]["branch_b"]["palette"]["id"] = 99;
  invalid_branch_b["scene"]["branch_b"]["palette"]["key"] = "invalid";
  std::array<char, 1024> invalid_branch_b_text{};
  const size_t invalid_branch_b_size = serializeJson(
      invalid_branch_b, invalid_branch_b_text.data(), invalid_branch_b_text.size());
  expect(!scene_json::parse_save(invalid_branch_b_text.data(),
                                 invalid_branch_b_size, save, error) &&
             error.code == scene_json::ErrorCode::InvalidScene &&
             error.validation_error ==
                 led::SceneValidationError::InvalidPaletteB,
         "branch-B palette failure was misreported as branch A");

  const char deep[] = R"json({"id":[[[[[[1]]]]]]})json";
  expect(!scene_json::parse_apply(deep, sizeof(deep) - 1U, apply, error) &&
             error.code == scene_json::ErrorCode::TooDeep,
         "JSON nesting beyond the public limit was accepted");

  std::array<char, scene_json::SCENE_JSON_BODY_MAX_BYTES + 1U> boundary{};
  std::memset(boundary.data(), ' ', boundary.size());
  std::memcpy(boundary.data(), "{\"id\":1}", 8);
  expect(scene_json::parse_apply(boundary.data(),
                                 scene_json::SCENE_JSON_BODY_MAX_BYTES,
                                 apply, error),
         "exactly 4096-byte JSON transport boundary was rejected");
  expect(!scene_json::parse_apply(boundary.data(), boundary.size(), apply,
                                  error),
         "4097-byte JSON transport boundary was accepted");
}

void test_export_import_round_trip() {
  const led::SceneBank original = sample_bank();
  JsonDocument exported;
  scene_json::build_export(exported, original, 9);
  std::array<char, scene_json::SCENE_JSON_BODY_MAX_BYTES + 1U> export_text{};
  const size_t export_size =
      serializeJson(exported, export_text.data(), export_text.size());
  expect(export_size > 0 && export_size < scene_json::SCENE_JSON_BODY_MAX_BYTES,
         "canonical export exceeded its bounded transport");
  static const char *const FORBIDDEN[] = {
      "ssid", "password", "pin", "latitude", "longitude", "fence",
      "home", "budget_ma", "channel_ma"};
  for (const char *token : FORBIDDEN) {
    expect(std::strstr(export_text.data(), token) == nullptr,
           "export leaked a non-visual field");
  }

  JsonDocument request;
  request["expected_generation"] = 0;
  request["dry_run"] = false;
  request["recover_corrupt"] = false;
  request["document"].set(exported.as<JsonVariantConst>());
  std::array<char, scene_json::SCENE_JSON_BODY_MAX_BYTES + 1U> request_text{};
  const size_t request_size =
      serializeJson(request, request_text.data(), request_text.size());
  scene_json::ImportRequest imported{};
  scene_json::Error error{};
  const bool imported_ok = scene_json::parse_import(
      request_text.data(), request_size, imported, error);
  if (!imported_ok) {
    std::fprintf(stderr, "import error=%s field=%s\n",
                 scene_json::error_name(error.code), error.field);
  }
  expect(imported_ok &&
             imported.has_expected_generation &&
             imported.expected_generation == 0 && !imported.dry_run &&
             imported.scene_count == led::SCENE_USER_SLOT_COUNT &&
             !imported.effects_registry_mismatch &&
             !imported.palettes_registry_mismatch &&
             led::scene_bank_equal(original, imported.bank),
         "export/import did not round-trip the semantic bank");

  JsonDocument too_many_document;
  too_many_document.set(exported.as<JsonVariantConst>());
  JsonArray too_many_scenes = too_many_document["scenes"].as<JsonArray>();
  JsonObject extra_scene = too_many_scenes.add<JsonObject>();
  extra_scene.set(too_many_scenes[0].as<JsonVariantConst>());
  request["document"].set(too_many_document.as<JsonVariantConst>());
  const size_t too_many_size =
      serializeJson(request, request_text.data(), request_text.size());
  expect(!scene_json::parse_import(request_text.data(), too_many_size,
                                   imported, error) &&
             error.code == scene_json::ErrorCode::InvalidValue,
         "import accepted more than four user scenes");

  JsonDocument duplicate_document;
  duplicate_document.set(exported.as<JsonVariantConst>());
  JsonArray duplicate_scenes = duplicate_document["scenes"].as<JsonArray>();
  duplicate_scenes.remove(3);
  JsonObject duplicate_scene = duplicate_scenes.add<JsonObject>();
  duplicate_scene.set(duplicate_scenes[0].as<JsonVariantConst>());
  request["document"].set(duplicate_document.as<JsonVariantConst>());
  const size_t duplicate_size =
      serializeJson(request, request_text.data(), request_text.size());
  expect(!scene_json::parse_import(request_text.data(), duplicate_size,
                                   imported, error) &&
             error.code == scene_json::ErrorCode::InvalidValue,
         "import accepted a duplicate user slot");

  request["document"].set(exported.as<JsonVariantConst>());

  request.remove("expected_generation");
  request["dry_run"] = true;
  request["document"]["registry"]["effects"] = 99;
  const size_t dry_size =
      serializeJson(request, request_text.data(), request_text.size());
  expect(scene_json::parse_import(request_text.data(), dry_size, imported,
                                  error) &&
             !imported.has_expected_generation && imported.dry_run &&
             imported.effects_registry_mismatch,
         "dry-run or informational registry mismatch contract changed");

  request["dry_run"] = false;
  const size_t missing_generation_size =
      serializeJson(request, request_text.data(), request_text.size());
  expect(!scene_json::parse_import(request_text.data(), missing_generation_size,
                                   imported, error) &&
             error.code == scene_json::ErrorCode::MissingField,
         "mutating import accepted a missing expected_generation");

  request["expected_generation"] = 0;
  request["document"]["schema_version"] = 2;
  const size_t future_size =
      serializeJson(request, request_text.data(), request_text.size());
  expect(!scene_json::parse_import(request_text.data(), future_size, imported,
                                   error) &&
             error.code == scene_json::ErrorCode::UnsupportedSchema,
         "future import schema was interpreted as v1");
}

}  // namespace

int main() {
  test_strict_mutation_parsers();
  test_export_import_round_trip();
  if (failures != 0) {
    std::fprintf(stderr, "scene_json_characterization: %d failure(s)\n",
                 failures);
    return 1;
  }
  std::puts("scene_json_characterization: ok");
  return 0;
}
