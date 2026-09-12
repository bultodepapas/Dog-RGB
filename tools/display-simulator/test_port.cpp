#include <Arduino.h>
#include <cstring>
#include "display/display.h"
#include "display/lvgl_port.h"
#include "display/inactivity.h"

display::DisplaySnapshot real_sample;
display::ConnectionSnapshot real_connection;
display::LedStatusSnapshot real_led;
namespace display {
DisplaySnapshot capture_snapshot() {
  auto value = real_sample; value.captured_ms = millis(); return value;
}
ConnectionSnapshot capture_connection() { return real_connection; }
LedStatusSnapshot capture_led_status() { return real_led; }
}
std::string report() { Print sink; display::report(sink); return sink.output; }
void has(const char *value) { assert(report().find(value) != std::string::npos); }
void pump(unsigned ms) {
  for (unsigned i = 0; i < ms; i += 5) { now_ms += 5; now_us += 5000; display::tick(); }
}
void command(char value) { Serial.input = value; display::tick(); }
void advance(uint32_t ms) { now_ms += ms; now_us += ms * 1000U; display::tick(); }
int main(int argc, char **) {
  display::InactivityTimer timer;
  assert(!timer.poll(UINT32_MAX)); // Disabled by default.
  timer.configure(30000, UINT32_MAX - 10000);
  assert(!timer.poll(19998)); assert(timer.poll(19999));
  assert(timer.expirations() == 1);
  assert(timer.poll(UINT32_MAX - 10000)); // Expired stays latched across another wrap.
  assert(timer.expirations() == 1);
  timer.activity(100); assert(!timer.poll(30099)); assert(timer.poll(30100));
  timer.configure(0, 30100); assert(!timer.poll(UINT32_MAX));
  if (argc > 1) {
    begin_result = false;
    assert(!display::begin()); assert(!display::begin());
    pump(2000); assert(begins == 1 && bitmap_calls == 0 && light_level == LOW);
    has("ready=0"); return 0;
  }
  assert(display::begin()); assert(display::begin());
  assert(begins == 1 && bitmap_pixels == 240 * 280 && light_level == HIGH);
  has("ui=lvgl"); has("demo=0");
  has("idle_ms=0 idle=0 timeouts=0");
  advance(31000); assert(light_level == HIGH); // Reboot default has no timeout.
  auto before = bitmap_calls;
  pump(2000); assert(bitmap_calls == before); // No redraw for an unchanged sample.
  command('f'); pump(10000);
  has("demo=1"); assert(bitmap_calls > before);
  assert(real_sample.gps_state == gps::ReceptionState::NoData && real_sample.distance_date == 0);
  command('b'); assert(light_level == LOW);
  before = bitmap_calls; pump(6000);
  assert(bitmap_calls > before && light_level == LOW);
  command('b'); assert(light_level == HIGH);
  command('d'); before = bitmap_calls; pump(6000);
  assert(bitmap_calls == before && light_level == LOW);
  command('f'); pump(2000); assert(bitmap_calls == before && light_level == LOW);
  command('d'); assert(bitmap_calls > before && light_level == HIGH);
  command('s'); has("ui=text"); before = bitmap_calls;
  pump(6000); assert(bitmap_calls == before && text_calls > 0);
  command('l'); has("ui=lvgl"); assert(light_level == LOW); pump(5); assert(bitmap_calls > before && light_level == HIGH);
  for (unsigned y = 0; y < 280; ++y) for (unsigned x = 0; x < 240; ++x)
    if (x < 24 || x >= 216 || y < 20 || y >= 264) assert(panel_frame[y * 240 + x] == 0);
  command('t'); before = bitmap_calls; pump(6000); assert(bitmap_calls == before);
  command('v'); has("demo=0"); pump(2000);
  const auto before_page_pixels = bitmap_pixels;
  command('c'); has("page=connection");
  // LVGL 8.4 transformed-area invalidation conservatively adds five pixels per side.
  assert(bitmap_pixels - before_page_pixels == 202 * 254);
  real_connection.ap_enabled = true;
  snprintf(real_connection.ap_ssid, sizeof(real_connection.ap_ssid), "%s", "DogRGB"); snprintf(real_connection.ap_ip, sizeof(real_connection.ap_ip), "%s", "192.168.4.1");
  before = bitmap_calls; pump(1100); assert(bitmap_calls > before);
  before = bitmap_calls; pump(1100); assert(bitmap_calls == before);
  // Physical input: bounce is ignored, one debounced release changes page.
  button_level = LOW; pump(10); button_level = HIGH; pump(40); has("page=connection");
  button_level = LOW; pump(80); button_level = HIGH; pump(40); has("page=status");
  command('n'); has("page=activity"); // Complete the new three-page cycle.
  button_level = LOW; pump(2000); button_level = HIGH; pump(40); has("page=activity");
  command('b'); assert(light_level == LOW);
  command('n'); has("page=activity"); assert(light_level == HIGH); // Wake, no page advance.
  command('n'); has("page=connection");
  command('d'); command('n'); has("page=connection"); has("enabled=1");
  assert(light_level == HIGH);
  command('e'); has("page=status");
  real_led.transport_enabled = true;
  real_led.intent = led::LedIntent::DayStatus;
  before = bitmap_calls; pump(1100); assert(bitmap_calls > before);
  before = bitmap_calls; pump(1100); assert(bitmap_calls == before);
  command('c');
  const auto connection_baseline = display::lvgl_port::stats().free_bytes;
  for (unsigned i = 0; i < 30; ++i) {
    command('n'); has("page=status"); command('n'); has("page=activity");
    command('n'); has("page=connection");
  }
  assert(display::lvgl_port::stats().free_bytes == connection_baseline);
  command('a');
  const auto baseline = display::lvgl_port::stats().free_bytes;
  for (unsigned i = 0; i < 30; ++i) { command('s'); command('l'); pump(5); }
  assert(display::lvgl_port::stats().free_bytes == baseline);
  // Real service: idle is independent of GPS samples, page redraw and rendering.
  command('v'); command('a'); command('i');
  advance(29999); assert(light_level == HIGH);
  advance(1); assert(light_level == LOW); has("idle_ms=30000 idle=1 timeouts=1");
  before = bitmap_calls;
  real_sample.gps_state = gps::ReceptionState::Fix;
  real_sample.speed_valid = true; real_sample.speed_kph = 7;
  advance(1100); assert(bitmap_calls > before && light_level == LOW);
  real_sample.gps_state = gps::ReceptionState::Stale;
  real_sample.speed_valid = false;
  advance(1100); assert(light_level == LOW);
  command('c'); has("page=connection"); assert(light_level == LOW);
  command('t'); command('a'); pump(5); assert(light_level == LOW);
  command('s'); command('l'); pump(5); assert(light_level == LOW);
  command('n'); has("page=activity"); assert(light_level == HIGH);
  command('n'); has("page=connection");
  const auto idle_baseline = display::lvgl_port::stats().free_bytes;
  for (const char page_command : {'a', 'c', 'e'}) {
    command(page_command);
    for (unsigned i = 0; i < 10; ++i) {
      command('i'); advance(30000); assert(light_level == LOW);
      command('n'); assert(light_level == HIGH);
      has(page_command == 'a' ? "page=activity" : page_command == 'c' ? "page=connection" : "page=status");
    }
  }
  command('c');
  assert(display::lvgl_port::stats().free_bytes == idle_baseline);
  // A debounced short release exactly at deadline must wake the selected page.
  command('i'); advance(29900);
  button_level = LOW; display::tick(); advance(35); advance(35);
  button_level = HIGH; display::tick(); advance(30);
  has("page=connection"); assert(light_level == HIGH);
  // A held button and a bounce do not extend the inactivity period.
  command('i'); advance(29900); button_level = LOW; display::tick();
  advance(100); assert(light_level == LOW);
  advance(2000); button_level = HIGH; display::tick(); advance(35);
  assert(light_level == LOW);
  button_level = LOW; pump(10); button_level = HIGH; pump(40);
  assert(light_level == LOW);
  button_level = LOW; pump(80); button_level = HIGH; pump(40);
  assert(light_level == HIGH); has("page=connection");
  // Disabled timer does not wake; next release still wakes without navigation.
  advance(30000); assert(light_level == LOW); command('o');
  advance(31000); assert(light_level == LOW); has("idle_ms=0 idle=0");
  command('n'); advance(31000); assert(light_level == HIGH); has("page=connection");
  command('i'); command('d'); advance(31000); has("enabled=0");
  command('n'); assert(light_level == HIGH); has("enabled=1"); has("page=connection");
  command('o'); command('a'); // Restore the GPS page for the existing clock-wrap check.
  command('r'); has("flushes=0"); has("pixels=0");
  now_ms = UINT32_MAX - 500; display::tick(); pump(1000);
  real_sample.gps_state = gps::ReceptionState::Fix;
  real_sample.speed_valid = true; real_sample.speed_kph = 3;
  pump(1100); assert(display::lvgl_port::stats().flushes > 0);
  Serial.input = "xxxxxxxxx"; display::tick(); assert(Serial.input == "x");
}
