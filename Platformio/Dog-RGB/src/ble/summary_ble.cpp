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
