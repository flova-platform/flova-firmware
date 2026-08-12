#include <Arduino.h>
#include <FlovaEsp32.h>

static FlovaClientConfig config = {nullptr, nullptr, nullptr};
static FlovaProvisioningConfig provisioning(nullptr, nullptr, nullptr,
                                            "universal_esp32", true);
FlovaEsp32 device(config, provisioning);

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp32 boot");
  device.begin();
}

void loop() {
  device.run();
}
