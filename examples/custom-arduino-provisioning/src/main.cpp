#include <Arduino.h>
#include <FlovaEsp32.h>

namespace {
char hardwareId[64] = {};
FlovaClientConfig LINK = {nullptr, nullptr, nullptr};
FlovaProvisioningConfig PROVISIONING(nullptr, nullptr, hardwareId, nullptr, true);

const uint8_t RELAY_PIN = 2;

struct RelayContext { uint8_t pin; } relayContext = {RELAY_PIN};

flova::WriteResult writeRelay(void* context, bool value) {
  RelayContext* relay = static_cast<RelayContext*>(context);
  digitalWrite(relay->pin, value ? HIGH : LOW);
  return flova::WriteResult::accept();
}

FlovaEsp32 client(LINK, PROVISIONING);
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
