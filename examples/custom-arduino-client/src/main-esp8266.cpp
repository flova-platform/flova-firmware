#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FlovaEsp8266.h>

namespace {
// The application keeps ownership of Wi-Fi, GPIO, timing, and the Arduino
// loop. Flova contributes private storage, TLS/Link, bounded state, and the
// datastream API without requiring a product ID or secret in source code.
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = LED_BUILTIN;
struct RelayContext { uint8_t pin; } relayContext = {RELAY_PIN};

flova::WriteResult writeRelay(void* context, bool value) {
  // User actions, schedules, and cloud automations all arrive here when they
  // target the "LED" datastream. A rejection leaves cached state unchanged.
  RelayContext* relay = static_cast<RelayContext*>(context);
  digitalWrite(relay->pin, value ? LOW : HIGH);
  return flova::WriteResult::accept();
}

FlovaEsp8266 client;
// This human-readable key is bound once to the server-assigned numeric ID.
// Runtime ESP8266 frames use the numeric ID to save memory and bandwidth.
flova::Datastream<bool> relay = client.datastream<bool>("LED");
bool lastReady = false;
}

void setup() {
  Serial.begin(115200);

  // Flova observes this connection; it does not change Wi-Fi mode or start a
  // setup AP in the normal SDK facade.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(relayContext.pin, OUTPUT);
  digitalWrite(relayContext.pin, HIGH);  // LED_BUILTIN is active-low.

  // Register the actuator handler before starting the runtime.
  relay.onWrite(writeRelay, &relayContext);

  // Link authentication continues cooperatively from the normal loop.
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // Keep running while offline; bounded reconnect is handled internally.
  client.run();
  const bool ready = client.ready();
  if (ready != lastReady) {
    Serial.println(ready ? "[flova] link ready" : "[flova] link offline");
    lastReady = ready;
  }

  // Leave time for the rest of the existing application.
  yield();
}
