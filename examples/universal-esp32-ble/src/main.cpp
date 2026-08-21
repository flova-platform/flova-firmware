#include <Arduino.h>
#include <FlovaUniversalEsp32Ble.h>

// BLE universal firmware is a separate setup-channel variant. It owns the
// temporary BLE provisioning service, Wi-Fi credentials, GPIO mappings, OTA,
// and restart policy; the existing universal-esp32 SoftAP target is unchanged.
FlovaUniversalEsp32Ble device;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp32 BLE universal boot");
  Serial.println("[flova] BLE Security 1 with null PoP: MVP only");
  device.begin();
}

void loop() { device.run(); }
