#include <Arduino.h>
#include "display/display.h"
#include "display/lvgl_port.h"

display::DisplaySnapshot real_sample;
namespace display {
DisplaySnapshot capture_snapshot() {
  auto value = real_sample; value.captured_ms = millis(); return value;
}
}
std::string report() { Print sink; display::report(sink); return sink.output; }
void has(const char *value) { assert(report().find(value) != std::string::npos); }
void pump(unsigned ms) {
  for (unsigned i = 0; i < ms; i += 5) { now_ms += 5; now_us += 5000; display::tick(); }
}
void command(char value) { Serial.input = value; display::tick(); }
int main(int argc, char **) {
  if (argc > 1) {
    begin_result = false;
    assert(!display::begin()); assert(!display::begin());
    pump(2000); assert(begins == 1 && bitmap_calls == 0 && light_level == LOW);
    has("ready=0"); return 0;
  }
  assert(display::begin()); assert(display::begin());
  assert(begins == 1 && bitmap_pixels == 240 * 280 && light_level == HIGH);
  has("ui=lvgl"); has("demo=0");
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
  command('l'); has("ui=lvgl"); assert(bitmap_calls > before);
  command('t'); before = bitmap_calls; pump(6000); assert(bitmap_calls == before);
  command('v'); has("demo=0"); pump(2000);
  const auto baseline = display::lvgl_port::stats().free_bytes;
  for (unsigned i = 0; i < 30; ++i) { command('s'); command('l'); }
  assert(display::lvgl_port::stats().free_bytes == baseline);
  command('r'); has("flushes=0"); has("pixels=0");
  now_ms = UINT32_MAX - 500; display::tick(); pump(1000);
  real_sample.gps_state = gps::ReceptionState::Fix;
  real_sample.speed_valid = true; real_sample.speed_kph = 3;
  pump(1100); assert(display::lvgl_port::stats().flushes > 0);
  Serial.input = "xxxxxxxxx"; display::tick(); assert(Serial.input == "x");
}
