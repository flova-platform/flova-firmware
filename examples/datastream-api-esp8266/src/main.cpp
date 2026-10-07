#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FlovaSDK.h>

// ESP8266 version of the helper-based datastream example. The SDK surface is the same;
// only board-owned Arduino includes and active-low LED behavior differ.
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = LED_BUILTIN;
const uint8_t RGB_PINS[3] = {5, 4, 14}; // GPIO5/4/14 on a common ESP8266 board.

FlovaEsp8266 client;
// Keys are developer-facing names. The server resolves them to stable compact
// IDs during binding, so normal ESP8266 runtime frames stay bounded.
auto temperature = FLOVA_DATASTREAM(client, float, "temperature");
auto relay = FLOVA_DATASTREAM(client, bool, "relay");
auto cookMode = FLOVA_DATASTREAM(client, flova::Text, "cook_mode");
auto lampColor = FLOVA_DATASTREAM(client, flova::Text, "lamp_color");
uint32_t lastSampleMs = 0;

void writeRelay(bool enabled) { digitalWrite(RELAY_PIN, enabled ? LOW : HIGH); }

flova::WriteResult writeCookMode(void*, flova::Text mode) {
  // Returning reject() keeps the previous value and revision authoritative.
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

  // The application owns Wi-Fi. Flova observes connection state and performs
  // its bounded private TLS/UTC work without taking over Wi-Fi mode.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(RELAY_PIN, OUTPUT);
  for (uint8_t pin : RGB_PINS) pinMode(pin, OUTPUT);
  FLOVA_ON_WRITE(relay, writeRelay);
  // Custom handlers remain available for application-specific validation such
  // as the bounded cook-mode values.
  FLOVA_ON_WRITE(cookMode, writeCookMode, nullptr);
  FLOVA_ON_WRITE(lampColor, applyColor);
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // This is where queued device commands become hardware writes. Keep it
  // responsive even when the application has other work to perform.
  client.run();
  if (millis() - lastSampleMs >= 1000) {
    lastSampleMs = millis();

    // FLOVA_REPORT() publishes an observation and does not invoke
    // FLOVA_ON_WRITE(). It is
    // also safe to use while the network is temporarily offline.
    FLOVA_REPORT(temperature, 25.0f);

    // Local logic uses the same validation path as a remote automation write.
    if (FLOVA_READ(temperature) > 30.0f) FLOVA_WRITE(relay, false);
  }
  yield();
}
