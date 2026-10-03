#include "display/identity_console.h"
#include "display/identity_store.h"
#include "storage/nvs_store.h"
#include <cassert>
#include <cstring>
#include <string>

namespace storage { Preferences test_prefs; Preferences &prefs_cfg() { return test_prefs; } }
int main() {
  display::IdentityConsole console;
  identity::load();
  auto send = [&](const std::string &body, const char *expected) {
    console.begin(100); const char *reply = nullptr;
    for (unsigned char ch : body + '\n') if (const auto r = console.feed(ch, 101)) reply = r;
    assert(reply && !strcmp(reply, expected) && !console.active());
  };
  const std::string valid = R"({"name":"FREYA","phone":"+100000000000","qr_kind":"whatsapp","expected_generation":0})";
  send("", "cancelled"); send("{}", "fields"); send("not-json", "fields");
  send(valid.substr(0, valid.size()-1), "fields");
  send(R"({"name":"FREYA\u0000X","phone":"+100000000000","qr_kind":"call","expected_generation":0})", "fields");
  send(R"({"name":"FREYA","phone":"+100000000000","qr_kind":"call","expected_generation":false})", "fields");
  send(R"({"name":"FREYA","phone":"+100000000000","qr_kind":"call","expected_generation":0,"extra":1})", "fields");
  send(std::string(513, 'n'), "body_size");
  send(std::string("n\0p", 3), "fields");
  assert(storage::test_prefs.writes == 0);
  console.begin(UINT32_MAX - 1000);
  console.feed('n', UINT32_MAX - 999);
  assert(!console.poll(3998)); assert(!strcmp(console.poll(3999), "timeout"));
  assert(console.active()); // Keep swallowing a delayed payload, not LCD commands.
  assert(!console.feed('p', 5000)); assert(!console.feed('\n', 5000)); assert(!console.active());
  console.begin(0); assert(!strcmp(console.feed('\n', 5000), "timeout"));
  assert(!console.active() && storage::test_prefs.writes == 0);
  send(valid, "saved"); assert(identity::configured() && identity::generation() == 1);
  send(valid, "conflict"); assert(storage::test_prefs.writes == 1);
  auto next = valid; next[next.size()-2] = '1';
  send(next + '\r', "unchanged"); assert(storage::test_prefs.writes == 1);
  auto invalid = next; invalid.replace(invalid.find("+100000000000"), 13, "123"); send(invalid, "phone");
  invalid = next; invalid.replace(invalid.find("FREYA"), 5, "Rene\\u0301"); send(invalid, "name");
  invalid = next; invalid.replace(invalid.find("whatsapp"), 8, "url"); send(invalid, "qr_kind");
  next.replace(next.find("FREYA"), 5, "LUNA");
  storage::test_prefs.fault = Preferences::Reject; send(next, "storage");
  assert(!strcmp(identity::get().name, "FREYA"));
  storage::test_prefs.fault = Preferences::None; send(next, "saved");
  identity::load(); assert(!strcmp(identity::get().name, "LUNA"));
  send(R"({"name":"","phone":"","qr_kind":"disabled","expected_generation":2})", "saved");
  assert(!identity::configured());
}
