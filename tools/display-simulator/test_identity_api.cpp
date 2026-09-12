#include <ArduinoJson.h>
#include "display/identity_store.h"
#include "storage/nvs_store.h"
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
using String = std::string;
struct Server {
  int status = 0;
  String input, output;
  bool body = true;
  bool hasArg(const char *) { return body; }
  String arg(const char *) { return input; }
  void send(int code, const char *, const String &value) { status = code; output = value; }
} server;
bool authorized = true;
void note_activity() {}
bool write_allowed() { if (!authorized) server.send(401, "application/json", "{}"); return authorized; }
namespace storage { Preferences test_prefs; Preferences &prefs_cfg() { return test_prefs; } }
// Exact current handler source, extracted at CMake configuration; not a model.
#include "identity_routes.inc"
int main() {
  identity::load();
  handle_identity_get(); assert(server.status == 200);
  JsonDocument state; assert(!deserializeJson(state, server.output));
#if DOG_RGB_DISPLAY_LVGL
  assert(state["supported"] == true && state["configured"] == false);
  const auto post = [](const char *body, int expected) {
    server.input = body; handle_identity_post(); assert(server.status == expected);
  };
  const char *valid = R"({"name":"FREYA","phone":"+1 (000) 000-00000","qr_kind":"whatsapp","expected_generation":0})";
  authorized = false; post(valid, 401); assert(storage::test_prefs.writes == 0); authorized = true;
  server.body = false; post(valid, 400); server.body = true;
  post("{}", 400); post("[]", 400); post("not-json", 400);
  post(R"({"name":"FREYA","phone":"+100000000000","qr_kind":"javascript:bad","expected_generation":0})",400);
  post(R"({"name":"Rene\u0301","phone":"+100000000000","qr_kind":"call","expected_generation":0})",400);
  post(R"({"name":"FREYA\u0000X","phone":"+100000000000","qr_kind":"call","expected_generation":0})",400);
  post(R"({"name":"FREYA","phone":"+100000000000","qr_kind":"call","expected_generation":false})",400);
  post(R"({"name":"FREYA","phone":"+100000000000","qr_kind":"call","expected_generation":-1})",400);
  post(R"({"name":"FREYA","phone":"+100000000000","qr_kind":"call","expected_generation":0,"extra":1})",400);
  post(std::string(513,' ').c_str(),413); assert(storage::test_prefs.writes == 0);
  post(valid,200); assert(!deserializeJson(state,server.output));
  assert(state["generation"] == 1 && state["phone"] == "+100000000000");
  assert(state["qr_payload"] == "https://wa.me/100000000000");
  post(valid,409);
  storage::test_prefs.fault = Preferences::Truncate;
  post(R"({"name":"NEW","phone":"+100000000001","qr_kind":"call","expected_generation":1})",500);
  handle_identity_get(); assert(!deserializeJson(state,server.output)); assert(state["name"] == "FREYA");
  storage::test_prefs.fault = Preferences::None;
  post(R"({"name":"Ren\u00e9","phone":"+100000000001","qr_kind":"call","expected_generation":1})",200);
  assert(!deserializeJson(state,server.output)); assert(state["qr_payload"] == "tel:+100000000001");
  post(R"({"name":"","phone":"","qr_kind":"disabled","expected_generation":2})",200);
  identity::load(); handle_identity_get(); assert(!deserializeJson(state,server.output));
  assert(state["configured"] == false && state["generation"] == 3);
#else
  assert(state["supported"] == false && state.size() == 2);
  handle_identity_post(); assert(server.status == 404 && storage::test_prefs.writes == 0);
#endif
  std::cout << "Identity actual HTTP handlers passed\n";
}
