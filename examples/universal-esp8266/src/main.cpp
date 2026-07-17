#include <Arduino.h>
#include <FlovaEsp8266.h>

FlovaEsp8266 device;

void setup() {
  Serial.begin(74880);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp8266 boot");
  device.begin();
}

void loop() {
  device.loop();
}
