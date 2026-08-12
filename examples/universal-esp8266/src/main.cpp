#include <Arduino.h>
#include <FlovaEsp8266.h>

static FlovaClientConfig config = {nullptr, nullptr, nullptr};
static FlovaProvisioningConfig provisioning(nullptr, nullptr, nullptr,
                                            "universal_esp8266", true);
FlovaEsp8266 device(config, provisioning);

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp8266 boot");
  const bool started = device.begin();
  Serial.printf("[flova] begin=%u lifecycle=%u provisioning=%u\n",
                started ? 1U : 0U,
                static_cast<unsigned>(device.lifecycle()),
                device.provisioning() ? 1U : 0U);
}

void loop() {
  device.run();
}
