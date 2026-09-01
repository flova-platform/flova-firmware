#include <Arduino.h>
#include <FlovaUniversalEsp32.h>

// Universal firmware is the no-code/full-device composition. Unlike the
// passive FlovaEsp32 facade, it owns setup SoftAP, Wi-Fi credential storage,
// universal GPIO mappings, OTA policy, and automatic restart policy.
// Development keeps the legacy open AP. Production must pass the stable,
// device-unique 8-63 character WPA password printed on the device label.
FlovaUniversalEsp32 device;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp32 boot");

  // A configured device restores private state. A fresh device enters the
  // universal provisioning lifecycle used by the mobile QR flow.
  device.begin();
}

void loop() {
  // This composition owns setup HTTP handling, Device Link, dynamic
  // datastream configuration, hardware mappings, OTA, and restart work.
  device.run();
}
