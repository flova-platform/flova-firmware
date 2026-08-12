#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FlovaEsp8266.h>

namespace {
// ESP8266 version of the datastream API example. The SDK surface is the same;
// only board-owned Arduino includes and active-low LED behavior differ.
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = LED_BUILTIN;
const uint8_t WATER_PIN = D5;

FlovaEsp8266 client;
// Keys are developer-facing names. The server resolves them to stable compact
// IDs during binding, so normal ESP8266 runtime frames stay bounded.
flova::Datastream<float> temperature = client.datastream<float>("temperature");
flova::Datastream<bool> relay = client.datastream<bool>("relay");
flova::Datastream<flova::Text> cookMode = client.datastream<flova::Text>("cook_mode");
uint32_t lastSampleMs = 0;

flova::WriteResult writeRelay(void*, bool enabled) {
  // Dashboard actions, user commands, schedules, and cloud automations all
  // reach this one callback. Safety is checked before touching the relay.
  if (enabled && digitalRead(WATER_PIN) == LOW)
    return flova::reject("insufficient_water");
  digitalWrite(RELAY_PIN, enabled ? LOW : HIGH);
  return flova::accept();
}

flova::WriteResult writeCookMode(void*, flova::Text mode) {
  // Returning reject() keeps the previous value and revision authoritative.
  const char* value = mode.c_str();
  return !strcmp(value, "start") || !strcmp(value, "pause") ||
                 !strcmp(value, "resume") || !strcmp(value, "cancel")
             ? flova::accept()
             : flova::reject("mode_not_supported");
}
}

void setup() {
  Serial.begin(115200);

  // The application owns Wi-Fi. Flova observes connection state and performs
  // its bounded private TLS/UTC work without taking over Wi-Fi mode.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(WATER_PIN, INPUT);
  // onWrite() is the command handler. Register it before client.begin().
  relay.onWrite(writeRelay, nullptr);
  cookMode.onWrite(writeCookMode, nullptr);
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // This is where queued device commands become hardware writes. Keep it
  // responsive even when the application has other work to perform.
  client.run();
  if (millis() - lastSampleMs >= 1000) {
    lastSampleMs = millis();

    // report() publishes an observation and does not invoke onWrite(). It is
    // also safe to use while the network is temporarily offline.
    temperature.report(25.0f);

    // Local logic uses the same validation path as a remote automation write.
    if (temperature.value() > 30.0f) relay.write(false);
  }
  yield();
}
