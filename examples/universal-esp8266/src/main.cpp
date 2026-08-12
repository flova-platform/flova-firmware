#include <Arduino.h>
#include <FlovaUniversalEsp8266.h>

// No-code universal firmware. It intentionally owns the whole board
// lifecycle: SoftAP provisioning, Wi-Fi credentials, dynamic datastream
// configuration, universal GPIO mappings, OTA, and automatic restart policy.
FlovaUniversalEsp8266 device;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp8266 boot");

  // Fresh devices enter setup AP mode. Configured devices restore validated
  // identity and reconnect without requiring a reboot after provisioning.
  const bool started = device.begin();
  Serial.printf("[flova] begin=%u lifecycle=%u provisioning=%u\n",
                started ? 1U : 0U,
                static_cast<unsigned>(device.lifecycle()),
                device.provisioning() ? 1U : 0U);
}

void loop() {
  // Keep this loop unblocked. It services setup/runtime Link, applies dynamic
  // hardware writes, and performs bounded OTA/restart work.
  device.run();
}
