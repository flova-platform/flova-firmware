#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FlovaEsp8266.h>

namespace {
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const FlovaClientConfig FLOVA = {
    "00000000-0000-0000-0000-000000000000",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "wss://engine.example.invalid/api/device-link"};
const uint8_t RELAY_PIN = LED_BUILTIN;
struct RelayContext { uint8_t pin; } relayContext = {RELAY_PIN};

flova::WriteResult writeRelay(void* context, bool value) {
  RelayContext* relay = static_cast<RelayContext*>(context);
  digitalWrite(relay->pin, value ? LOW : HIGH);
  return flova::WriteResult::accept();
}

FlovaEsp8266 client(FLOVA);
flova::Datastream<bool> relay = client.datastream<bool>("LED");
bool lastReady = false;
}

void setup() {
  Serial.begin(115200);
  pinMode(relayContext.pin, OUTPUT);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  relay.mode(flova::Mode::Command).onWrite(writeRelay, &relayContext);
  if (!client.begin()) Serial.println("[flova] client configuration failed");
}

void loop() {
  client.run();
  const bool ready = client.ready();
  if (ready != lastReady) {
    Serial.println(ready ? "[flova] link ready" : "[flova] link offline");
    lastReady = ready;
  }
  yield();
}
