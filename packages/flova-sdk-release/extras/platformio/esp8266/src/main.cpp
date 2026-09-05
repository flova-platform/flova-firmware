#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FlovaEsp8266.h>

FlovaEsp8266 flovaDevice;
auto relay = flovaDevice.datastream<bool>("relay");

void setup() {
  WiFi.begin("your-wifi", "your-password");
  pinMode(LED_BUILTIN, OUTPUT);
  relay.onWrite([](bool enabled) {
    digitalWrite(LED_BUILTIN, enabled ? LOW : HIGH);
  });
  flovaDevice.begin();
}

void loop() { flovaDevice.run(); }
