"""Real configuration codec/A-B save/load and LED apply across fresh processes."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_gps_bringup import ARDUINO, NEOPIXEL_STUB

ROOT = Path(__file__).resolve().parents[1]
PREFERENCES = r'''
#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <fstream>
class Preferences {
 public:
  std::map<std::string,std::vector<uint8_t>> data;
  bool short_write = false;
  size_t getBytesLength(const char *key) { return data[key].size(); }
  size_t getBytes(const char *key, void *dst, size_t cap) {
    const auto &v=data[key]; if(v.size()>cap) return 0;
    memcpy(dst,v.data(),v.size()); return v.size();
  }
  size_t putBytes(const char *key,const void *src,size_t n) {
    size_t written=short_write?n/2:n;const auto *p=static_cast<const uint8_t*>(src);
    data[key]=std::vector<uint8_t>(p,p+written);return written;
  }
  bool getBool(const char *key,bool fallback) {
    return data[key].empty()?fallback:data[key][0]!=0;
  }
  size_t putBool(const char *key,bool value) { uint8_t b=value; return putBytes(key,&b,1); }
  uint8_t getUChar(const char *,uint8_t fallback) { return fallback; }
  uint16_t getUShort(const char *,uint16_t fallback) { return fallback; }
  float getFloat(const char *,float fallback) { return fallback; }
  String getString(const char *,const char *fallback) { return fallback; }
  void read_file(const char *path) {
    std::ifstream in(path,std::ios::binary); if(!in) return;
    uint32_t key_size=0,size=0;
    while(in.read(reinterpret_cast<char*>(&key_size),4)) {
      assert(key_size<64);std::string key(key_size,'\0');in.read(&key[0],key_size);
      in.read(reinterpret_cast<char*>(&size),4);assert(size<512);
      std::vector<uint8_t> v(size);in.read(reinterpret_cast<char*>(v.data()),size);
      assert(in.good());data[key]=v;
    }
  }
  void write_file(const char *path) {
    std::ofstream out(path,std::ios::binary);
    for(const auto &entry:data) {
      uint32_t key_size=entry.first.size(),size=entry.second.size();
      out.write(reinterpret_cast<char*>(&key_size),4);out.write(entry.first.data(),key_size);
      out.write(reinterpret_cast<char*>(&size),4);out.write(reinterpret_cast<const char*>(entry.second.data()),size);
    }
    assert(out.good());
  }
};
'''
HARNESS = r'''
#include <cmath>
#include <cstdlib>
#include "config/runtime_config.h"
#include "storage/nvs_store.h"
#include "led/led_bus.h"
#include "led/led_ui.h"
#include "wifi/wifi_mgr.h"
#include "pins.h"
Preferences prefs;
led::LedBus bus(LED_STRIP_COUNT,PIN_LED_A_DATA,PIN_LED_B_DATA,true);
unsigned applies=0;
namespace storage { Preferences &prefs_cfg() { return ::prefs; } }
namespace wifi_mgr { void apply_mdns(const String&, const String&) {} }
namespace led_ui {
void apply_brightness(uint8_t b) { ++applies;bus.set_brightness(b); }
void apply_power_config(bool e,uint16_t b,uint16_t base,uint8_t rgb,uint8_t w) {
  bus.configure_power({e,b,base,rgb,w});
}
}
void matches(unsigned value) {
  const auto &cfg=config::get();
  assert(cfg.mode==value%4 && cfg.brightness==40+value);
  assert(cfg.single.speed==70+value && cfg.gps_min_sats==6);
  assert(cfg.led_power_budget_ma==1500 && !cfg.led_power_limit_enabled);
  assert(cfg.ap_ssid=="I4-native" && cfg.mdns=="i4-native");
}
int main(int argc,char **argv) {
  assert(argc==4);prefs.read_file(argv[1]);config::load();
  const unsigned value=static_cast<unsigned>(atoi(argv[3]));
  if(std::string(argv[2])=="read") { matches(value); return 0; }
  if(std::string(argv[2])=="fault") {
    matches(value);const auto previous=config::get();
    const auto generation=config::storage_generation();
    const auto failures=config::storage_save_failures();
    config::get_mut().brightness=99;prefs.short_write=true;
    assert(!config::save());
    assert(config::storage_generation()==generation && config::storage_save_failures()==failures+1);
    config::get_mut()=previous; // Same rollback contract used by the HTTP handler.
    assert(applies==0);prefs.write_file(argv[1]);return 0;
  }
  const RuntimeConfig previous=config::get();
  auto &cfg=config::get_mut();
  cfg.mode=value%4;cfg.brightness=40+value;cfg.single.speed=70+value;
  cfg.gps_min_sats=6;cfg.led_power_budget_ma=1500;cfg.led_power_limit_enabled=false;
  cfg.ap_ssid="I4-native";cfg.mdns="i4-native";
  const auto generation=config::storage_generation();const auto slot=config::storage_slot();
  assert(config::save());assert(config::storage_generation()==generation+1);
  assert(config::storage_slot()!=slot);
  bus.begin(255);config::apply(previous);assert(applies==1);
  matches(value);
  for(const auto &strip:strips) {
#if DOG_RGB_BRINGUP_STAGE == 3
    assert(strip.brightness==16); // Applied bench cap never replaces stored 40+value.
#else
    assert(strip.brightness==40+value);
#endif
  }
  prefs.write_file(argv[1]);
}
'''


class NativeConfigTests(unittest.TestCase):
    def test_ten_changes_fresh_process_reloads_and_interrupted_write(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        for variant in ("classic", "display"):
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                (folder / "Arduino.h").write_text(ARDUINO + '\n#include <cmath>\nusing std::isfinite;\nclass IPAddress;\n', encoding="utf-8")
                (folder / "Preferences.h").write_text(PREFERENCES, encoding="utf-8")
                (folder / "Adafruit_NeoPixel.h").write_text(
                    NEOPIXEL_STUB.replace('assert(brightness == 16);', '').replace('assert(brightness <= 16);', '').replace('assert(strips[slot].brightness == 16);', ''), encoding="utf-8")
                (folder / "config.h").write_text(
                    '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wunused-variable"\n'
                    f'#include "{(ROOT / "include/config.h").as_posix()}"\n#pragma GCC diagnostic pop\n', encoding="utf-8")
                (folder / "test.cpp").write_text(HARNESS, encoding="utf-8")
                flags = ["-DDOG_RGB_BOARD_XIAO_S3=1"] if variant == "classic" else [
                    "-DDOG_RGB_BOARD_WAVESHARE_LCD169_V2=1", "-DDOG_RGB_BRINGUP_STAGE=3", "-DDOG_RGB_DISPLAY_ENABLED=1"]
                executable = folder / "test.exe"
                sources = ("config/runtime_config.cpp", "led/led_bus.cpp", "led/led_color.cpp",
                           "led/power_limiter.cpp", "led/effect_registry.cpp", "led/palette_registry.cpp")
                result = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    *flags, "-I", str(folder), "-I", str(ROOT / "include"), str(folder / "test.cpp"),
                    *[str(ROOT / "src" / s) for s in sources], "-o", str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                storage = str(folder / "preferences.bin")
                for value in range(10):
                    for operation in ("save", "read"):
                        result = subprocess.run([str(executable), storage, operation, str(value)], capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for operation in ("fault", "read"):
                    result = subprocess.run([str(executable), storage, operation, "9"], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
