#include "led/scene_runtime.h"

#include <Arduino.h>
#include <esp_system.h>

#include "config.h"
#include "storage/scene_nvs_backend.h"

namespace scene_runtime {
namespace {

storage::SceneNvsBackend backend_instance;
led::SceneCatalog catalog_instance;
storage::SceneStore store_instance(backend_instance, catalog_instance);
led::ScenePlayer player_instance(catalog_instance, SHOW_SCENE_MS);
SceneRuntimeDiagnostics runtime_diagnostics{};
uint32_t last_led_frame_us = 0;
bool write_gap_pending = false;

struct StoreCounters {
  uint32_t mutations;
  uint32_t writes_failed;
  uint32_t verifies_failed;
  uint32_t uncertain;
};

static StoreCounters counters() {
  const storage::SceneStoreDiagnostics &value = store_instance.diagnostics();
  return StoreCounters{value.mutation_count, value.write_failures,
                       value.verify_failures, value.uncertain_commits};
}

static bool counters_changed(const StoreCounters &before,
                             const StoreCounters &after) {
  return before.mutations != after.mutations ||
         before.writes_failed != after.writes_failed ||
         before.verifies_failed != after.verifies_failed ||
         before.uncertain != after.uncertain;
}

static void observe_operation(uint32_t started_us,
                              const StoreCounters &before,
                              bool import_operation) {
  const uint32_t duration_us = micros() - started_us;
  const StoreCounters after = counters();
  if (!counters_changed(before, after)) return;
  store_instance.observe_write_duration(duration_us);
  write_gap_pending = true;
  if (import_operation) {
    runtime_diagnostics.last_import_us = duration_us;
    if (duration_us > runtime_diagnostics.max_import_us) {
      runtime_diagnostics.max_import_us = duration_us;
    }
  } else {
    runtime_diagnostics.last_save_us = duration_us;
    if (duration_us > runtime_diagnostics.max_save_us) {
      runtime_diagnostics.max_save_us = duration_us;
    }
  }
}

} // namespace

void begin(uint32_t now_ms, uint8_t configured_mode) {
  store_instance.load();
  player_instance.reset(now_ms, configured_mode);
  player_instance.seed(esp_random());
  runtime_diagnostics.initialized = true;
}

void tick(uint32_t now_ms, uint8_t configured_mode, bool show_mode,
          bool body_permitted) {
  player_instance.tick(now_ms, configured_mode, show_mode, body_permitted);
}

bool request_apply(uint8_t scene_id) {
  return player_instance.request_apply(scene_id);
}

void request_cancel() {
  player_instance.request_cancel();
}

storage::SceneStoreResult save_slot(uint8_t slot_one_based,
                                    const led::SceneV1 &scene,
                                    uint32_t expected_generation) {
  const StoreCounters before = counters();
  const uint32_t started_us = micros();
  const storage::SceneStoreResult result = store_instance.save_slot(
      slot_one_based, scene, expected_generation);
  observe_operation(started_us, before, false);
  return result;
}

storage::SceneStoreResult delete_slot(uint8_t slot_one_based,
                                      uint32_t expected_generation) {
  const StoreCounters before = counters();
  const uint32_t started_us = micros();
  const storage::SceneStoreResult result =
      store_instance.delete_slot(slot_one_based, expected_generation);
  observe_operation(started_us, before, false);
  return result;
}

storage::SceneStoreResult replace_all(const led::SceneBank &bank,
                                      uint32_t expected_generation,
                                      bool recover_corrupt) {
  const StoreCounters before = counters();
  const uint32_t started_us = micros();
  const storage::SceneStoreResult result = store_instance.replace_all(
      bank, expected_generation, recover_corrupt);
  observe_operation(started_us, before, true);
  return result;
}

const led::SceneCatalog &catalog() {
  return catalog_instance;
}

const led::ScenePlayer &player() {
  return player_instance;
}

const storage::SceneStore &store() {
  return store_instance;
}

const SceneRuntimeDiagnostics &diagnostics() {
  return runtime_diagnostics;
}

void note_led_frame(uint32_t now_us) {
  if (write_gap_pending && last_led_frame_us != 0U) {
    const uint32_t gap_us = now_us - last_led_frame_us;
    if (gap_us > runtime_diagnostics.max_led_gap_during_write_us) {
      runtime_diagnostics.max_led_gap_during_write_us = gap_us;
    }
    write_gap_pending = false;
  }
  last_led_frame_us = now_us;
}

} // namespace scene_runtime
