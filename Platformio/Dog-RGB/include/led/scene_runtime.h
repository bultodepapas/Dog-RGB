#pragma once

#include <stdint.h>

#include "led/scene_player.h"
#include "storage/scene_store.h"

namespace scene_runtime {

struct SceneRuntimeDiagnostics {
  bool initialized;
  uint32_t last_save_us;
  uint32_t max_save_us;
  uint32_t last_import_us;
  uint32_t max_import_us;
  uint32_t max_led_gap_during_write_us;
};

void begin(uint32_t now_ms, uint8_t configured_mode);
void tick(uint32_t now_ms, uint8_t configured_mode, bool show_mode,
          bool body_permitted);

bool request_apply(uint8_t scene_id);
void request_cancel();
storage::SceneStoreResult save_slot(uint8_t slot_one_based,
                                    const led::SceneV1 &scene,
                                    uint32_t expected_generation);
storage::SceneStoreResult delete_slot(uint8_t slot_one_based,
                                      uint32_t expected_generation);
storage::SceneStoreResult replace_all(const led::SceneBank &bank,
                                      uint32_t expected_generation,
                                      bool recover_corrupt);

const led::SceneCatalog &catalog();
const led::ScenePlayer &player();
const storage::SceneStore &store();
const SceneRuntimeDiagnostics &diagnostics();

// Called after a physical LED frame. It measures the actual frame-to-frame gap
// surrounding a synchronous scene-store write, including HTTP and NVS costs.
void note_led_frame(uint32_t now_us);

} // namespace scene_runtime
