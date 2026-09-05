#include <Arduino.h>
#include <WiFi.h>
#include <FlovaEsp32.h>

FlovaEsp32 flovaDevice;
auto relay = flovaDevice.datastream<bool>("relay");

void setup() {
  WiFi.begin("your-wifi", "your-password");
  pinMode(2, OUTPUT);
  relay.onWrite([](bool enabled) { digitalWrite(2, enabled ? HIGH : LOW); });
  flovaDevice.begin();
}

void loop() { flovaDevice.run(); }
