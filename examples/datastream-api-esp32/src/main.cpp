#include <Arduino.h>
#include <WiFi.h>
#include <FlovaEsp32.h>

namespace {
const FlovaClientConfig CONFIG = {
    "00000000-0000-0000-0000-000000000000",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "wss://engine.example.invalid/api/device-link"};
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = 2;
const uint8_t WATER_PIN = 34;

FlovaEsp32 client(CONFIG);
flova::Datastream<float> temperature = client.datastream<float>("temperature");
flova::Datastream<bool> relay = client.datastream<bool>("relay");
flova::Datastream<const char*> cookMode = client.datastream<const char*>("cook_mode");
uint32_t lastSampleMs = 0;

flova::ReadResult<float> readTemperature() {
  return flova::ReadResult<float>::success(25.0f);
}

flova::WriteResult writeRelay(void*, bool enabled) {
  if (enabled && digitalRead(WATER_PIN) == LOW)
    return flova::WriteResult::reject("insufficient_water");
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
  return flova::WriteResult::accept();
}

flova::WriteResult writeCookMode(void*, const char* mode) {
  return mode && (!strcmp(mode, "start") || !strcmp(mode, "pause") ||
                  !strcmp(mode, "resume") || !strcmp(mode, "cancel"))
             ? flova::WriteResult::accept()
             : flova::WriteResult::reject("mode_not_supported");
}
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(WATER_PIN, INPUT);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  temperature.mode(flova::Mode::Sample).offline(flova::OfflinePolicy::Drop).onRead(readTemperature);
  relay.persist(flova::PersistencePolicy::Persistent).onWrite(writeRelay, nullptr);
  cookMode.mode(flova::Mode::Command).offline(flova::OfflinePolicy::Reject).onWrite(writeCookMode, nullptr);
  client.begin();
}

void loop() {
  client.run();
  if (millis() - lastSampleMs >= 1000) {
    lastSampleMs = millis();
    temperature.refresh();
    if (temperature.read() > 30.0f) relay.write(false);
  }
  yield();
}
