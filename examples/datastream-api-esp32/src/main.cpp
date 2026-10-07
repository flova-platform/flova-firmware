#include <Arduino.h>
#include <WiFi.h>
#include <FlovaSDK.h>

// This example focuses on the datastream API:
//   FLOVA_REPORT()    = sensor or externally observed state;
//   FLOVA_ON_WRITE()  = user/cloud/automation/schedule commands;
//   FLOVA_WRITE()     = local application commands;
//   FLOVA_READ()      = local cached state, never a network request.
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = 2;
const uint8_t RGB_PINS[3] = {25, 26, 27}; // Match these to your LED wiring.

FlovaEsp32 client;
// These human-readable keys are used for declarations and API/configuration.
// Engine binds them once to compact numeric runtime IDs.
auto temperature = FLOVA_DATASTREAM(client, float, "temperature");
auto relay = FLOVA_DATASTREAM(client, bool, "relay");
auto cookMode = FLOVA_DATASTREAM(client, flova::Text, "cook_mode");
auto lampColor = FLOVA_DATASTREAM(client, flova::Text, "lamp_color");
uint32_t lastSampleMs = 0;

void writeRelay(bool enabled) { digitalWrite(RELAY_PIN, enabled ? HIGH : LOW); }

flova::WriteResult writeCookMode(void*, flova::Text mode) {
  // Reject unsupported bounded text values without changing cached state,
  // revision, persistence, or the outgoing state report.
  const char* value = mode.c_str();
  return !strcmp(value, "start") || !strcmp(value, "pause") ||
                 !strcmp(value, "resume") || !strcmp(value, "cancel")
             ? flova::accept()
             : flova::reject("mode_not_supported");
}

flova::WriteResult applyColor(flova::Text value) {
  flova::RgbColor color;
  if (!flova::parseRgbHex(value, color)) return flova::reject("invalid_color");
  analogWrite(RGB_PINS[0], color.r);
  analogWrite(RGB_PINS[1], color.g);
  analogWrite(RGB_PINS[2], color.b);
  return flova::accept();
}

void setup() {
  Serial.begin(115200);

  // Wi-Fi is owned by this application. Flova observes it and keeps the rest
  // of the application's services untouched.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(RELAY_PIN, OUTPUT);
  for (uint8_t pin : RGB_PINS) pinMode(pin, OUTPUT);
  // Register handlers before starting the SDK runtime. Remote commands are
  // applied from client.run(), never directly from a transport callback.
  FLOVA_ON_WRITE(relay, writeRelay);
  FLOVA_ON_WRITE(cookMode, writeCookMode, nullptr);
  FLOVA_ON_WRITE(lampColor, applyColor);
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // Keep this frequent so Link input, acknowledgements, retries, and remote
  // hardware commands remain responsive.
  client.run();
  if (millis() - lastSampleMs >= 1000) {
    lastSampleMs = millis();

    // report() means "the application observed this value". It does not call
    // FLOVA_ON_WRITE() because a sensor observation is not an actuator command.
    FLOVA_REPORT(temperature, 25.0f);

    // This is local application logic. The same effect could be configured as
    // an Engine automation, which would eventually arrive at writeRelay().
    if (FLOVA_READ(temperature) > 30.0f) FLOVA_WRITE(relay, false);
  }
  yield();
}
