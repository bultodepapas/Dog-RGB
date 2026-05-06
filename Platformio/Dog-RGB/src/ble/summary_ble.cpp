#include "ble/summary_ble.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "gps/gps.h"

namespace summary_ble {
namespace {
BLECharacteristic *summary_char = nullptr;
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
  adv->start();
}

void tick() {
  if (summary_char == nullptr) {
    return;
  }
  uint8_t payload[16];
  gps::build_summary_payload(payload, sizeof(payload));
  summary_char->setValue(payload, sizeof(payload));
}
} // namespace summary_ble
