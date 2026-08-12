#include <Arduino.h>
#include <WiFi.h>
#include <FlovaEsp32.h>

namespace {
// This example focuses on the datastream API:
//   report()  = sensor or externally observed state;
//   onWrite() = user/cloud/automation/schedule commands;
//   write()   = local application commands;
//   value()   = local cached state, never a network request.
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = 2;
const uint8_t WATER_PIN = 34;

FlovaEsp32 client;
// These human-readable keys are used for declarations and API/configuration.
// Engine binds them once to compact numeric runtime IDs.
flova::Datastream<float> temperature = client.datastream<float>("temperature");
flova::Datastream<bool> relay = client.datastream<bool>("relay");
flova::Datastream<flova::Text> cookMode = client.datastream<flova::Text>("cook_mode");
uint32_t lastSampleMs = 0;

flova::WriteResult writeRelay(void*, bool enabled) {
  // Engine evaluates automations and schedules, then sends their resulting
  // write here just like a dashboard or mobile-user command. The device still
  // performs the final safety check before touching hardware.
  if (enabled && digitalRead(WATER_PIN) == LOW)
    return flova::reject("insufficient_water");
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
  return flova::accept();
}

flova::WriteResult writeCookMode(void*, flova::Text mode) {
  // Reject unsupported bounded text values without changing cached state,
  // revision, persistence, or the outgoing state report.
  const char* value = mode.c_str();
  return !strcmp(value, "start") || !strcmp(value, "pause") ||
                 !strcmp(value, "resume") || !strcmp(value, "cancel")
             ? flova::accept()
             : flova::reject("mode_not_supported");
}
}

void setup() {
  Serial.begin(115200);

  // Wi-Fi is owned by this application. Flova observes it and keeps the rest
  // of the application's services untouched.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(WATER_PIN, INPUT);
  // Register handlers before starting the SDK runtime. Remote commands are
  // applied from client.run(), never directly from a transport callback.
  relay.onWrite(writeRelay, nullptr);
  cookMode.onWrite(writeCookMode, nullptr);
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // Keep this frequent so Link input, acknowledgements, retries, and remote
  // hardware commands remain responsive.
  client.run();
  if (millis() - lastSampleMs >= 1000) {
    lastSampleMs = millis();

    // report() means "the application observed this value". It does not call
    // relay.onWrite() because a sensor observation is not an actuator command.
    temperature.report(25.0f);

    // This is local application logic. The same effect could be configured as
    // an Engine automation, which would eventually arrive at writeRelay().
    if (temperature.value() > 30.0f) relay.write(false);
  }
  yield();
}
