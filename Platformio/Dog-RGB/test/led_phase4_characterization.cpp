#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "led/palette_registry.h"
#include "led/scene.h"
#include "led/scene_catalog.h"
#include "led/scene_player.h"
#include "storage/scene_store.h"
#include "util/crc32.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

void write_u16_le(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

led::SceneV1 user_scene(uint8_t slot, const char *name,
                        uint8_t speed_delta = 0) {
  led::SceneCatalog catalog;
  led::SceneV1 scene = catalog.builtin_at(0);
  scene.scene_id = led::scene_id_from_slot(slot);
  scene.speed = static_cast<uint8_t>(scene.speed + speed_delta);
  const bool named = led::scene_set_name(scene, name, std::strlen(name));
  expect(named, "test fixture name must be valid UTF-8");
  return scene;
}

led::SceneBank bank_with(const led::SceneV1 &scene, uint8_t slot) {
  led::SceneBank bank{};
  led::scene_bank_clear(bank);
  bank.slots[slot - 1U] = scene;
  bank.slots[slot - 1U].scene_id = led::scene_id_from_slot(slot);
  bank.occupied_mask = static_cast<uint8_t>(1U << (slot - 1U));
  return bank;
}

class FakeSceneBackend final : public storage::SceneRecordBackend {
 public:
  struct Bank {
    std::array<uint8_t, storage::SCENE_RECORD_READ_MAX + 32U> bytes{};
    size_t size = 0;
    bool exists = false;
    bool read_error = false;
  };

  storage::SceneBackendRead read(uint8_t bank_index, uint8_t *out,
                                 size_t capacity,
                                 size_t &actual_size) override {
    ++read_calls;
    if (bank_index > 1U || banks[bank_index].read_error) {
      actual_size = 0;
      return storage::SceneBackendRead::Error;
    }
    const Bank &bank = banks[bank_index];
    if (!bank.exists) {
      actual_size = 0;
      return storage::SceneBackendRead::Missing;
    }
    actual_size = bank.size;
    if (bank.size > capacity) return storage::SceneBackendRead::TooLarge;
    std::memcpy(out, bank.bytes.data(), bank.size);
    return storage::SceneBackendRead::Ok;
  }

  storage::SceneBackendWrite write(uint8_t bank_index, const uint8_t *data,
                                   size_t size,
                                   size_t &written_size) override {
    ++write_calls;
    if (bank_index > 1U || fail_next_write) {
      fail_next_write = false;
      written_size = 0;
      return storage::SceneBackendWrite::Error;
    }
    Bank &bank = banks[bank_index];
    const bool forced_cut = next_write_limit < size;
    const size_t stored = forced_cut
                              ? next_write_limit
                              : (torn_next_write && size > 5U ? size - 5U
                                                              : size);
    std::memcpy(bank.bytes.data(), data, stored);
    bank.size = stored;
    bank.exists = true;
    written_size = stored;
    if (corrupt_next_write && stored > 20U) bank.bytes[20] ^= 0x5AU;
    const bool success = !torn_next_write && !forced_cut;
    torn_next_write = false;
    corrupt_next_write = false;
    next_write_limit = storage::SCENE_RECORD_BYTES;
    return success ? storage::SceneBackendWrite::Ok
                   : storage::SceneBackendWrite::Error;
  }

  size_t free_entries() const override { return available_entries; }

  void preload(uint8_t bank_index, const led::SceneBank &bank,
               uint32_t generation) {
    banks[bank_index].exists = storage::encode_scene_record(
        bank, generation, banks[bank_index].bytes.data(),
        storage::SCENE_RECORD_BYTES);
    banks[bank_index].size = storage::SCENE_RECORD_BYTES;
  }

  Bank banks[2]{};
  bool fail_next_write = false;
  bool torn_next_write = false;
  bool corrupt_next_write = false;
  size_t next_write_limit = storage::SCENE_RECORD_BYTES;
  size_t available_entries = 42;
  uint32_t read_calls = 0;
  uint32_t write_calls = 0;
};

void test_scene_contract_and_catalog() {
  expect(led::SCENE_SCHEMA_VERSION == 1 &&
             led::SCENE_REGISTRY_VERSION == 1 &&
             led::SCENE_WIRE_BYTES == 44 &&
             led::SCENE_NAME_BYTES == 24,
         "scene binary contract changed without a version bump");
  expect(led::scene_id_from_slot(1) == 128 &&
             led::scene_id_from_slot(4) == 131 &&
             led::scene_id_from_slot(0) == led::SCENE_ID_INVALID,
         "stable user scene ids changed");

  led::SceneCatalog catalog;
  static const char *const keys[led::SCENE_BUILTIN_COUNT] = {
      "high_visibility", "calm", "active", "party"};
  for (uint8_t i = 0; i < led::SCENE_BUILTIN_COUNT; ++i) {
    const led::SceneV1 &scene = catalog.builtin_at(i);
    expect(scene.scene_id == i + 1U && led::scene_validate(scene),
           "built-in scene is not valid/addressable");
    expect(std::strcmp(led::scene_key(scene.scene_id), keys[i]) == 0,
           "built-in scene key is not stable");
    expect(scene.show_eligible,
           "curated built-in unexpectedly disappeared from Show");
  }

  led::SceneV1 original = user_scene(1, "Paseo cálido");
  uint8_t wire[led::SCENE_WIRE_BYTES]{};
  led::SceneValidationError error = led::SceneValidationError::None;
  expect(led::scene_encode(original, wire, sizeof(wire), &error),
         "valid scene did not encode");
  expect(wire[0] == 128 && wire[1] == 3 && wire[8] == 255 &&
             wire[9] == 0x90 && wire[10] == 0x01 && wire[41] == 0 &&
             wire[42] == 0 && wire[43] == 0,
         "scene wire layout is not canonical little-endian V1");
  led::SceneV1 decoded{};
  expect(led::scene_decode(wire, sizeof(wire), decoded, &error) &&
             led::scene_semantic_equal(original, decoded) &&
             led::scene_fingerprint(original) != 0,
         "scene codec did not round-trip semantically");

  std::array<uint8_t, led::SCENE_WIRE_BYTES> malformed{};
  std::memcpy(malformed.data(), wire, sizeof(wire));
  malformed[1] |= 0x80U;
  expect(!led::scene_decode(malformed.data(), malformed.size(), decoded,
                            &error) &&
             error == led::SceneValidationError::InvalidWire,
         "unknown scene flag bits were accepted");
  std::memcpy(malformed.data(), wire, sizeof(wire));
  malformed[43] = 1;
  expect(!led::scene_decode(malformed.data(), malformed.size(), decoded,
                            &error),
         "non-zero reserved scene bytes were accepted");

  led::SceneV1 invalid = original;
  std::memset(invalid.name, 0, sizeof(invalid.name));
  invalid.name[0] = static_cast<char>(0xC0);
  invalid.name[1] = static_cast<char>(0xAF);
  expect(!led::scene_validate(invalid, &error) &&
             error == led::SceneValidationError::InvalidUtf8,
         "overlong UTF-8 scene name was accepted");
  invalid = original;
  invalid.name[23] = 'x';
  expect(!led::scene_validate(invalid, &error) &&
             error == led::SceneValidationError::InvalidName,
         "non-canonical bytes after the name terminator were accepted");
  invalid = original;
  invalid.effect_b = 2;
  invalid.palette_b = led::PALETTE_NIGHT_RED;
  expect(!led::scene_validate(invalid, &error) &&
             error == led::SceneValidationError::MirrorMismatch,
         "mirror scene accepted divergent branches");
  invalid = original;
  invalid.effect_a = 7;
  invalid.effect_b = 7;
  invalid.palette_a = led::PALETTE_NONE;
  invalid.palette_b = led::PALETTE_NONE;
  expect(!led::scene_validate(invalid, &error) &&
             error == led::SceneValidationError::AdvancedShow,
         "advanced effect was accepted into automatic Show rotation");
  invalid.show_eligible = false;
  expect(led::scene_validate(invalid),
         "advanced effect should remain available for explicit scenes");
}

void test_record_codec_and_generation_order() {
  const led::SceneBank source = bank_with(user_scene(1, "Registro"), 1);
  std::array<uint8_t, storage::SCENE_RECORD_BYTES> bytes{};
  expect(storage::encode_scene_record(source, 17, bytes.data(), bytes.size()),
         "valid scene bank did not encode");
  expect(std::memcmp(bytes.data(), "SCN1", 4) == 0 && bytes[4] == 1 &&
             bytes[5] == 0 && bytes[6] == 196 && bytes[7] == 0 &&
             bytes[8] == 17 && bytes[14] == 1,
         "record header is not the documented V1 format");
  storage::DecodedSceneRecord decoded{};
  expect(storage::decode_scene_record(bytes.data(), bytes.size(), decoded) ==
                 storage::SceneRecordStatus::Valid &&
             decoded.generation == 17 &&
             led::scene_bank_equal(decoded.bank, source),
         "scene record did not round-trip");

  bytes[20] ^= 0x01U;
  expect(storage::decode_scene_record(bytes.data(), bytes.size(), decoded) ==
             storage::SceneRecordStatus::Corrupt,
         "record with invalid CRC was accepted");

  expect(storage::scene_generation_is_newer(1, UINT32_MAX) &&
             !storage::scene_generation_is_newer(UINT32_MAX, 1) &&
             storage::scene_generation_is_ambiguous(
                 1, UINT32_C(0x80000001)),
         "serial-number generation arithmetic is not wrap-safe");
}

void test_store_atomicity_and_optimistic_concurrency() {
  FakeSceneBackend backend;
  led::SceneCatalog catalog;
  storage::SceneStore store(backend, catalog);
  expect(store.load() == storage::SceneStoreHealth::Empty &&
             store.generation() == 0 &&
             store.diagnostics().free_entries == 42,
         "empty store did not initialize safely");

  led::SceneV1 first = user_scene(1, "Primera");
  storage::SceneStoreResult result = store.save_slot(1, first, 0);
  expect(result.code == storage::SceneStoreResultCode::Ok &&
             result.generation == 1 && backend.write_calls == 1 &&
             store.diagnostics().active_bank == 0 &&
             catalog.find(led::scene_id_from_slot(1)) != nullptr,
         "first mutation was not verified and published from bank A");

  result = store.save_slot(1, first, 1);
  expect(result.code == storage::SceneStoreResultCode::NoChange &&
             result.no_change && result.generation == 1 &&
             backend.write_calls == 1,
         "semantic no-op consumed flash or advanced generation");
  result = store.save_slot(1, user_scene(1, "Conflicto", 1), 0);
  expect(result.code == storage::SceneStoreResultCode::GenerationConflict &&
             backend.write_calls == 1,
         "stale writer bypassed optimistic concurrency");

  led::SceneV1 second = user_scene(1, "Segunda", 2);
  result = store.save_slot(1, second, 1);
  expect(result.code == storage::SceneStoreResultCode::Ok &&
             result.generation == 2 &&
             store.diagnostics().active_bank == 1,
         "second mutation did not alternate to bank B");

  backend.torn_next_write = true;
  result = store.save_slot(1, user_scene(1, "Interrumpida", 3), 2);
  expect(result.code == storage::SceneStoreResultCode::WriteFailed &&
             store.generation() == 2 &&
             store.health() == storage::SceneStoreHealth::Recovered &&
             led::scene_semantic_equal(*catalog.user_at(1), second),
         "torn write replaced the last verified snapshot");

  led::SceneCatalog reboot_catalog;
  storage::SceneStore rebooted(backend, reboot_catalog);
  expect(rebooted.load() == storage::SceneStoreHealth::Recovered &&
             rebooted.generation() == 2 &&
             led::scene_semantic_equal(*reboot_catalog.user_at(1), second),
         "reboot did not recover the surviving A/B bank");
}

void test_store_corruption_future_and_recovery() {
  const led::SceneBank older = bank_with(user_scene(1, "Anterior"), 1);
  const led::SceneBank newer = bank_with(user_scene(1, "Nueva", 5), 1);

  FakeSceneBackend corrupt_newest;
  corrupt_newest.preload(0, older, 10);
  corrupt_newest.preload(1, newer, 11);
  corrupt_newest.banks[1].bytes[50] ^= 1U;
  led::SceneCatalog recovered_catalog;
  storage::SceneStore recovered(corrupt_newest, recovered_catalog);
  expect(recovered.load() == storage::SceneStoreHealth::Recovered &&
             recovered.generation() == 10 &&
             led::scene_bank_equal(recovered.bank(), older),
         "corrupt newest bank did not fall back to last verified bank");

  FakeSceneBackend degraded_backend;
  degraded_backend.banks[0].exists = true;
  degraded_backend.banks[0].size = 7;
  std::memcpy(degraded_backend.banks[0].bytes.data(), "garbage", 7);
  led::SceneCatalog degraded_catalog;
  storage::SceneStore degraded(degraded_backend, degraded_catalog);
  expect(degraded.load() == storage::SceneStoreHealth::DegradedEmpty,
         "missing+corrupt banks did not enter explicit degraded-empty state");
  storage::SceneStoreResult result =
      degraded.save_slot(1, user_scene(1, "Recuperada"), 0);
  expect(result.code == storage::SceneStoreResultCode::Ok &&
             result.generation == 1,
         "degraded-empty store could not heal by overwriting corrupt bank");

  FakeSceneBackend future_backend;
  future_backend.preload(0, older, 4);
  write_u16_le(future_backend.banks[0].bytes.data() + 4, 2);
  write_u32_le(
      future_backend.banks[0].bytes.data() + storage::SCENE_RECORD_BYTES - 4,
      util::crc32_ieee(future_backend.banks[0].bytes.data(),
                       storage::SCENE_RECORD_BYTES - 4));
  led::SceneCatalog future_catalog;
  storage::SceneStore future(future_backend, future_catalog);
  expect(future.load() == storage::SceneStoreHealth::ReadOnlyFuture,
         "credible future record was not preserved read-only");
  result = future.save_slot(1, user_scene(1, "No sobrescribir"), 0);
  expect(result.code == storage::SceneStoreResultCode::ReadOnly &&
             future_backend.write_calls == 0,
         "older firmware overwrote a future record");

  FakeSceneBackend oversized_backend;
  oversized_backend.banks[1].exists = true;
  oversized_backend.banks[1].size = storage::SCENE_RECORD_READ_MAX + 1;
  led::SceneCatalog oversized_catalog;
  storage::SceneStore oversized(oversized_backend, oversized_catalog);
  expect(oversized.load() == storage::SceneStoreHealth::OversizedUnknown,
         "oversized record was not protected from destructive downgrade");
}

void test_store_wrap_and_ambiguous_recovery() {
  const led::SceneBank before_wrap =
      bank_with(user_scene(1, "Antes wrap"), 1);
  const led::SceneBank after_wrap =
      bank_with(user_scene(1, "Después wrap", 1), 1);
  FakeSceneBackend wrap_backend;
  wrap_backend.preload(0, before_wrap, UINT32_MAX);
  wrap_backend.preload(1, after_wrap, 1);
  led::SceneCatalog wrap_catalog;
  storage::SceneStore wrap_store(wrap_backend, wrap_catalog);
  expect(wrap_store.load() == storage::SceneStoreHealth::Healthy &&
             wrap_store.generation() == 1 &&
             led::scene_bank_equal(wrap_store.bank(), after_wrap),
         "generation wrap selected the stale bank");

  FakeSceneBackend ambiguous_backend;
  ambiguous_backend.preload(0, before_wrap, 1);
  ambiguous_backend.preload(1, after_wrap, UINT32_C(0x80000001));
  led::SceneCatalog ambiguous_catalog;
  storage::SceneStore ambiguous(ambiguous_backend, ambiguous_catalog);
  expect(ambiguous.load() == storage::SceneStoreHealth::Ambiguous &&
             ambiguous.generation() == 0 &&
             ambiguous_catalog.user_at(1) == nullptr,
         "half-range generation conflict was guessed instead of quarantined");
  storage::SceneStoreResult result =
      ambiguous.save_slot(1, user_scene(1, "Bloqueada"), 0);
  expect(result.code == storage::SceneStoreResultCode::RecoveryRequired,
         "ordinary mutation bypassed explicit ambiguous-store recovery");

  led::SceneBank recovered_bank{};
  led::scene_bank_clear(recovered_bank);
  recovered_bank = bank_with(user_scene(1, "Elegida"), 1);
  result = ambiguous.replace_all(recovered_bank, 0, true);
  expect(result.code == storage::SceneStoreResultCode::Ok,
         "explicit recovery did not replace ambiguous banks");
  led::SceneCatalog reboot_catalog;
  storage::SceneStore rebooted(ambiguous_backend, reboot_catalog);
  expect(rebooted.load() == storage::SceneStoreHealth::Healthy &&
             led::scene_bank_equal(rebooted.bank(), recovered_bank),
         "ambiguous recovery was not durable across reboot");
}

void test_store_thousand_deterministic_power_cycles() {
  FakeSceneBackend backend;
  for (uint32_t step = 0; step < 1000U; ++step) {
    led::SceneCatalog before_catalog;
    storage::SceneStore before(backend, before_catalog);
    before.load();
    const led::SceneBank previous = before.bank();
    const uint32_t previous_generation = before.generation();

    led::SceneV1 scene = user_scene(
        1, "Secuencia", static_cast<uint8_t>((step % 127U) + 1U));
    led::SceneBank candidate = previous;
    candidate.slots[0] = scene;
    candidate.occupied_mask |= 1U;

    const uint32_t fault = (step * UINT32_C(2654435761)) >> 29U;
    backend.fail_next_write = fault == 1U;
    backend.torn_next_write = fault == 2U;
    backend.corrupt_next_write = fault == 3U;
    const storage::SceneStoreResult result =
        before.save_slot(1, scene, previous_generation);

    led::SceneCatalog reboot_catalog;
    storage::SceneStore rebooted(backend, reboot_catalog);
    const storage::SceneStoreHealth health = rebooted.load();
    expect(health != storage::SceneStoreHealth::Ambiguous &&
               health != storage::SceneStoreHealth::Uncertain &&
               health != storage::SceneStoreHealth::Corrupt,
           "injected single-bank failure produced an unknowable reboot state");
    const bool is_previous = led::scene_bank_equal(rebooted.bank(), previous);
    const bool is_candidate = led::scene_bank_equal(rebooted.bank(), candidate);
    expect(is_previous || is_candidate,
           "power-cycle sequence produced a partially mixed scene bank");
    if (result.code == storage::SceneStoreResultCode::Ok) {
      expect(is_candidate && rebooted.generation() == result.generation,
             "acknowledged commit was not durable after immediate reboot");
    }
  }
}

void test_store_every_record_cut_preserves_previous_generation() {
  for (size_t cut = 0; cut < storage::SCENE_RECORD_BYTES; ++cut) {
    FakeSceneBackend backend;
    const led::SceneBank generation_one =
        bank_with(user_scene(1, "Generación uno"), 1);
    const led::SceneBank previous =
        bank_with(user_scene(1, "Generación dos", 1), 1);
    backend.preload(0, generation_one, 1);
    backend.preload(1, previous, 2);

    led::SceneCatalog catalog;
    storage::SceneStore store(backend, catalog);
    expect(store.load() == storage::SceneStoreHealth::Healthy &&
               store.generation() == 2,
           "record-cut fixture did not select generation two");

    backend.next_write_limit = cut;
    const storage::SceneStoreResult result = store.save_slot(
        1, user_scene(1, "Candidata", 2), store.generation());
    expect(result.code != storage::SceneStoreResultCode::Ok,
           "partial record write was acknowledged");

    led::SceneCatalog reboot_catalog;
    storage::SceneStore rebooted(backend, reboot_catalog);
    const storage::SceneStoreHealth health = rebooted.load();
    expect((health == storage::SceneStoreHealth::Recovered ||
            health == storage::SceneStoreHealth::Healthy) &&
               rebooted.generation() == 2 &&
               led::scene_bank_equal(rebooted.bank(), previous),
           "record cut replaced or mixed the previous durable generation");
  }
}

void test_scene_player_commands_stale_and_show() {
  led::SceneCatalog catalog;
  led::SceneBank users = bank_with(user_scene(1, "Manual"), 1);
  expect(catalog.replace_users(users, 1), "player fixture catalog is invalid");
  led::ScenePlayer player(catalog, 100);
  player.reset(0, 1);

  expect(!player.request_apply(250),
         "player accepted a scene absent from the catalog");
  expect(player.request_apply(led::scene_id_from_slot(1)),
         "player rejected an existing user scene");
  player.tick(10, 1, false, true);
  const uint32_t first_revision = player.activation_revision();
  expect(player.playback() == led::ScenePlayback::Manual &&
             player.active_scene_id() == led::scene_id_from_slot(1) &&
             player.applied_generation() == 1 && !player.stale(),
         "manual apply was not published at the next tick boundary");

  led::SceneBank unrelated = users;
  unrelated.slots[1] = user_scene(2, "Otra");
  unrelated.occupied_mask |= 0x02U;
  expect(catalog.replace_users(unrelated, 2),
         "unrelated scene edit fixture is invalid");
  player.tick(20, 1, false, true);
  expect(!player.stale(),
         "unrelated catalog generation made active snapshot stale");

  led::SceneBank edited = unrelated;
  edited.slots[0].speed++;
  expect(catalog.replace_users(edited, 3),
         "active scene edit fixture is invalid");
  player.tick(30, 1, false, true);
  expect(player.stale() && player.active_scene()->speed == users.slots[0].speed,
         "active immutable snapshot changed in place or missed staleness");

  expect(player.request_apply(led::scene_id_from_slot(1)),
         "active scene could not be re-applied");
  player.tick(40, 1, false, true);
  expect(!player.stale() && player.active_scene()->speed == edited.slots[0].speed &&
             player.activation_revision() > first_revision,
         "re-apply did not restart from the newest scene snapshot");
  player.tick(50, 2, false, true);
  expect(player.playback() == led::ScenePlayback::None,
         "configured mode change did not cancel manual override");

  expect(player.request_apply(1) && player.request_apply(2),
         "built-in scenes were not command-addressable");
  player.request_cancel();
  player.tick(60, 2, false, true);
  expect(player.playback() == led::ScenePlayback::None &&
             player.diagnostics().superseded_commands == 2 &&
             player.diagnostics().cancel_count == 1,
         "last-command-wins queue is not deterministic");

  player.reset(1000, 3);
  player.seed(UINT32_C(0x12345678));
  player.tick(1000, 3, true, true);
  const uint8_t first_show = player.active_scene_id();
  expect(player.playback() == led::ScenePlayback::Show && first_show != 0,
         "Show did not select an eligible scene");
  player.tick(1200, 3, true, false);
  expect(player.active_scene_id() == first_show &&
             player.show_elapsed_ms() == 0,
         "Show advanced while body rendering was preempted");
  player.tick(1250, 3, true, true);
  expect(player.active_scene_id() == first_show &&
             player.show_elapsed_ms() == 50,
         "Show active-time clock included paused wall time");
  player.tick(1300, 3, true, true);
  const uint8_t second_show = player.active_scene_id();
  expect(second_show != 0 && second_show != first_show,
         "Show repeated immediately at a scene boundary");
  player.tick(1400, 3, false, true);
  expect(player.playback() == led::ScenePlayback::None,
         "leaving configured Show mode did not clear Show playback");
}

void test_scene_player_full_bag_and_millis_wrap() {
  led::SceneCatalog catalog;
  led::SceneBank users{};
  led::scene_bank_clear(users);
  for (uint8_t slot = 1; slot <= led::SCENE_USER_SLOT_COUNT; ++slot) {
    char name[12] = {};
    std::snprintf(name, sizeof(name), "Usuario %u", slot);
    users.slots[slot - 1U] = user_scene(slot, name, slot);
    users.occupied_mask |= static_cast<uint8_t>(1U << (slot - 1U));
  }
  expect(catalog.replace_users(users, 1),
         "eight-scene Show fixture is invalid");
  led::ScenePlayer player(catalog, 10);
  player.reset(100, 2);
  player.seed(UINT32_C(0xA5A55A5A));
  bool seen[256]{};
  uint8_t distinct = 0;
  uint32_t now = 100;
  for (uint8_t i = 0; i < led::SCENE_CATALOG_CAPACITY; ++i) {
    player.tick(now, 2, true, true);
    const uint8_t id = player.active_scene_id();
    if (id != 0U && !seen[id]) {
      seen[id] = true;
      distinct++;
    }
    now += 10U;
  }
  expect(distinct == led::SCENE_CATALOG_CAPACITY,
         "Show bag omitted or repeated a scene before exhausting eight IDs");
  const uint8_t last_cycle = player.active_scene_id();
  player.tick(now, 2, true, true);
  expect(player.active_scene_id() != last_cycle,
         "new Show bag repeated the previous cycle boundary immediately");

  led::SceneCatalog wrap_catalog;
  led::ScenePlayer wrap_player(wrap_catalog, 100);
  const uint32_t near_wrap = UINT32_MAX - 50U;
  wrap_player.reset(near_wrap, 2);
  wrap_player.tick(near_wrap, 2, true, true);
  const uint8_t before_wrap = wrap_player.active_scene_id();
  wrap_player.tick(25U, 2, true, true);
  expect(wrap_player.active_scene_id() == before_wrap &&
             wrap_player.show_elapsed_ms() == 76U,
         "Show active-time clock failed across millis() wrap");
  wrap_player.tick(50U, 2, true, true);
  expect(wrap_player.active_scene_id() != before_wrap,
         "Show duration did not expire correctly after millis() wrap");
}

}  // namespace

int main() {
  test_scene_contract_and_catalog();
  test_record_codec_and_generation_order();
  test_store_atomicity_and_optimistic_concurrency();
  test_store_corruption_future_and_recovery();
  test_store_wrap_and_ambiguous_recovery();
  test_store_thousand_deterministic_power_cycles();
  test_store_every_record_cut_preserves_previous_generation();
  test_scene_player_commands_stale_and_show();
  test_scene_player_full_bag_and_millis_wrap();
  if (failures != 0) {
    std::fprintf(stderr, "led_phase4_characterization: %d failure(s)\n",
                 failures);
    return 1;
  }
  std::puts("led_phase4_characterization: ok");
  return 0;
}
