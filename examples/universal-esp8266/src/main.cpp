#include <Arduino.h>
#include <FlovaUniversalEsp8266.h>

// No-code universal firmware. It intentionally owns the whole board
// lifecycle: SoftAP provisioning, Wi-Fi credentials, dynamic datastream
// configuration, universal GPIO mappings, OTA, and automatic restart policy.
// Development keeps the legacy open AP. Production must pass the stable,
// device-unique 8-63 character WPA password printed on the device label.
FlovaUniversalEsp8266 device;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp8266 boot");
  flova::logTlsHeap("boot", flova::tlsHeapStats());

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
  static uint32_t lastReport = 0;
  static uint32_t minimumHeap = UINT32_MAX;
  static uint32_t minimumBlock = UINT32_MAX;
  static uint32_t minimumStack = UINT32_MAX;
  const auto heap = flova::tlsHeapStats();
  if (heap.dramFree < minimumHeap) minimumHeap = heap.dramFree;
  if (heap.dramMaxBlock < minimumBlock) minimumBlock = heap.dramMaxBlock;
  if (heap.stackFree < minimumStack) minimumStack = heap.stackFree;
  if (millis() - lastReport >= 5000) {
    lastReport = millis();
    flova::logTlsHeap("sample", heap);
    Serial.printf_P(PSTR("[flova] memory minimum_heap=%u minimum_block=%u minimum_stack=%u lifecycle=%u\n"),
        minimumHeap, minimumBlock, minimumStack, static_cast<unsigned>(device.lifecycle()));
  }
}
