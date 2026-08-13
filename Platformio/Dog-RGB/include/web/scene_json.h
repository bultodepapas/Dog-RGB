#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "led/scene_catalog.h"

namespace scene_json {

static const size_t SCENE_JSON_BODY_MIN_BYTES = 2;
static const size_t SCENE_JSON_BODY_MAX_BYTES = 4096;
// Import adds one request wrapper above the exported document. With ArduinoJson
// counting the root container, the deepest valid path is
// request.document.scenes[].branch.effect (6 containers).
static const uint8_t SCENE_JSON_NESTING_LIMIT = 6;

enum class ErrorCode : uint8_t {
  None = 0,
  InvalidJson,
  TooDeep,
  ExpectedObject,
  MissingField,
  UnknownField,
  InvalidType,
  InvalidValue,
  InvalidScene,
  ReferenceMismatch,
  UnsupportedSchema,
};

struct Error {
  ErrorCode code;
  led::SceneValidationError validation_error;
  char field[64];
};

struct ApplyRequest {
  uint8_t scene_id;
};

struct SaveRequest {
  uint32_t expected_generation;
  uint8_t slot;
  led::SceneV1 scene;
};

struct DeleteRequest {
  uint32_t expected_generation;
  uint8_t slot;
};

struct ImportRequest {
  bool has_expected_generation;
  uint32_t expected_generation;
  bool dry_run;
  bool recover_corrupt;
  led::SceneBank bank;
  uint8_t scene_count;
  bool effects_registry_mismatch;
  bool palettes_registry_mismatch;
};

bool parse_apply(const char *data, size_t size, ApplyRequest &out,
                 Error &error);
bool parse_cancel(const char *data, size_t size, Error &error);
bool parse_save(const char *data, size_t size, SaveRequest &out,
                Error &error);
bool parse_delete(const char *data, size_t size, DeleteRequest &out,
                  Error &error);
bool parse_import(const char *data, size_t size, ImportRequest &out,
                  Error &error);

void append_scene(JsonObject out, const led::SceneV1 &scene,
                  bool include_identity, uint8_t slot = 0);
void build_export(JsonDocument &document, const led::SceneBank &bank,
                  uint32_t store_generation);

const char *error_name(ErrorCode code);

} // namespace scene_json
