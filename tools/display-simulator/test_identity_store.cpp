#include "display/identity_store.h"
#include "storage/nvs_store.h"
#include "util/crc32.h"
#include <cassert>
#include <cstring>
#include <iostream>

namespace storage { Preferences test_prefs; Preferences &prefs_cfg() { return test_prefs; } }
int main() {
  using Result = identity::SaveResult;
  using Kind = display::QrContactKind;
  auto &prefs = storage::test_prefs;
  assert(identity::save("FREYA", "+100000000000", Kind::WhatsApp, 0) == Result::NotLoaded);
  identity::load(); assert(!identity::configured() && identity::generation() == 0 && prefs.writes == 0);
  assert(identity::save("", "", Kind::Disabled, 0) == Result::Unchanged);
  assert(identity::save("Rene\xcc\x81", "+100000000000", Kind::WhatsApp, 0) == Result::InvalidName);
  assert(identity::save("FREYA", "100000000000", Kind::WhatsApp, 0) == Result::InvalidPhone);
  assert(identity::save("FREYA", "+100000000000", static_cast<Kind>(99), 0) == Result::InvalidKind);
  assert(prefs.writes == 0);
  assert(identity::save("FREYA", "+1 (000) 000-00000", Kind::WhatsApp, 0) == Result::Saved);
  assert(identity::generation() == 1 && prefs.blobs["id_a"].size() == 84);
  assert(!strcmp(identity::get().phone, "+100000000000"));
  assert(identity::save("FREYA", "+100000000000", Kind::WhatsApp, 1) == Result::Unchanged && prefs.writes == 1);
  assert(identity::save("NEW", "+100000000000", Kind::WhatsApp, 0) == Result::Conflict && prefs.writes == 1);
  identity::load(); assert(identity::configured() && !strcmp(identity::get().name, "FREYA"));
  for (auto fault : {Preferences::Reject, Preferences::Truncate, Preferences::Corrupt}) {
    prefs.fault = fault;
    assert(identity::save("NEW", "+100000000001", Kind::Call, 1) == Result::Storage);
    assert(!strcmp(identity::get().name, "FREYA"));
    identity::load(); assert(identity::generation() == 1 && !strcmp(identity::get().phone, "+100000000000"));
  }
  prefs.fault = Preferences::None;
  assert(identity::save("Ren\xc3\xa9", "+100000000001", Kind::Call, 1) == Result::Saved);
  assert(prefs.blobs["id_b"].size() == 84);
  identity::load(); assert(identity::generation() == 2 && !strcmp(identity::get().name, "Ren\xc3\xa9"));
  // Corrupted newest bank recovers the older complete identity.
  prefs.blobs["id_b"][30] ^= 1;
  identity::load(); assert(identity::generation() == 1 && !strcmp(identity::get().name, "FREYA"));
  assert(identity::save("", "", Kind::Disabled, 1) == Result::Saved);
  identity::load(); assert(!identity::configured() && identity::generation() == 2);
  // A successful write followed by a readback I/O error is indeterminate:
  // RAM stays at the verified version; reboot may select the new complete bank.
  prefs.fault = Preferences::Readback;
  assert(identity::save("FREYA", "+100000000000", Kind::WhatsApp, 2) == Result::Storage);
  assert(!identity::configured());
  identity::load(); assert(identity::configured() && identity::generation() == 3);
  prefs.fault = Preferences::None;
  // Wrap-safe selection, then generation rollover through zero.
  auto &bank = prefs.blobs["id_a"];
  uint32_t generation = 0xffffffffU;
  memcpy(bank.data() + 8, &generation, 4);
  uint32_t crc = util::crc32_ieee(bank.data(), 80); memcpy(bank.data() + 80, &crc, 4);
  prefs.blobs["id_b"].clear(); identity::load();
  assert(identity::generation() == 0xffffffffU);
  assert(identity::save("NEXT", "+100000000000", Kind::Disabled, generation) == Result::Saved);
  identity::load(); assert(identity::generation() == 0 && !strcmp(identity::get().name, "NEXT"));
  // Even a correctly checksummed record cannot smuggle noncanonical data.
  auto &invalid = prefs.blobs["id_b"];
  invalid[12] = 0xff;
  crc = util::crc32_ieee(invalid.data(), 80); memcpy(invalid.data() + 80, &crc, 4);
  identity::load(); assert(identity::generation() == 0xffffffffU);
  std::cout << "Identity real store: restart, corruption, write faults, clear and rollover passed\n";
}
