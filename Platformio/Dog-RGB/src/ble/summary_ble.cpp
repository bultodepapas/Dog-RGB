#include "ble/summary_ble.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "gps/gps.h"
#include "wifi/wifi_mgr.h"

namespace summary_ble {
namespace {
BLECharacteristic *summary_char = nullptr;
bool ble_adv_active = false;
static const char *BLE_DEVICE_NAME = "Dog-Collar";
static const char *BLE_SERVICE_UUID = "8b4c0001-6c1d-4f3c-a5b0-1e0c5a00a101";
static const char *BLE_CHAR_UUID = "8b4c0002-6c1d-4f3c-a5b0-1e0c5a00a101";
}

void begin() {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(BLE_SERVICE_UUID);
  summary_char = service->createCharacteristic(BLE_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
  summary_char->setValue("init");
  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  // Bug fix: ESP32-S3 official docs classify SoftAP + BLE as C1 (performance
  // unstable). The default advertising interval (20-40 ms) aggressively competes
  // with WiFi AP beacons (~102 ms period), causing beacon gaps that make the
  // SSID appear/disappear on phones. Slowing advertising to 500-1000 ms gives
  // the radio scheduler enough breathing room to send WiFi beacons reliably.
  adv->setMinInterval(800);  // 800 * 0.625 ms = 500 ms
  adv->setMaxInterval(1600); // 1600 * 0.625 ms = 1000 ms
  // Do NOT call adv->start() here. tick() manages advertising based on AP state
  // so BLE never competes with WiFi beacons when the AP is active.
  ble_adv_active = false;
}

void tick() {
  if (summary_char == nullptr) {
    return;
  }

  // On ESP32-S3, WiFi and BLE share a single PCB antenna. Espressif's own
  // coexistence table marks SoftAP + BLE advertising as C1 (unstable):
  // BLE advertising packets starve WiFi beacons, making the SSID invisible.
  // Solution: only advertise BLE when the AP is off.
  const bool ap_on = wifi_mgr::ap_enabled();
  if (ap_on && ble_adv_active) {
    BLEDevice::getAdvertising()->stop();
    ble_adv_active = false;
    Serial.println("[BLE] adv paused — AP active");
  } else if (!ap_on && !ble_adv_active) {
    BLEDevice::getAdvertising()->start();
    ble_adv_active = true;
    Serial.println("[BLE] adv resumed — AP off");
  }

  uint8_t payload[16];
  gps::build_summary_payload(payload, sizeof(payload));
  summary_char->setValue(payload, sizeof(payload));
}
} // namespace summary_ble
