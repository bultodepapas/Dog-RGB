#include <Arduino.h>
#include "pins.h"

static const unsigned long HEARTBEAT_MS = 500;
static unsigned long last_heartbeat_ms = 0;
static bool led_state = false;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  Serial.println("Dog-RGB ESP32-S3 base firmware");
}

void loop() {
  const unsigned long now_ms = millis();

  if (now_ms - last_heartbeat_ms >= HEARTBEAT_MS) {
    last_heartbeat_ms = now_ms;
    led_state = !led_state;
    digitalWrite(PIN_STATUS_LED, led_state ? HIGH : LOW);
    Serial.println("heartbeat");
  }

  // Placeholder for IMU sampling and LED animation update.
}
