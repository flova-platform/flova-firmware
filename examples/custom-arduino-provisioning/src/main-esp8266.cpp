#include <Arduino.h>
#include <FlovaEsp8266.h>

namespace {
FlovaClientConfig LINK = {nullptr, nullptr, nullptr};
FlovaProvisioningConfig PROVISIONING(nullptr, nullptr, nullptr,
                                     "custom_arduino_esp8266", true);
const uint8_t RELAY_PIN = LED_BUILTIN;
struct RelayContext { uint8_t pin; } relayContext = {RELAY_PIN};

flova::WriteResult writeRelay(void* context, bool value) {
  RelayContext* relay = static_cast<RelayContext*>(context);
  digitalWrite(relay->pin, value ? LOW : HIGH);
  return flova::WriteResult::accept();
}

FlovaEsp8266 client(LINK, PROVISIONING);
flova::Datastream<bool> relay = client.datastream<bool>("LED");
}

void setup() {
  Serial.begin(115200);
  pinMode(relayContext.pin, OUTPUT);
  relay.mode(flova::Mode::Command).onWrite(writeRelay, &relayContext);
  if (!client.begin()) Serial.println("[flova] provisioning startup failed");
}

void loop() {
  client.run();
  yield();
}
