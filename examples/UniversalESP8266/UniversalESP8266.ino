#include <Arduino.h>
#include <FlovaUniversalEsp8266.h>

FlovaUniversalEsp8266 device;
void setup() {
  Serial.begin(115200);
  device.begin();
}
void loop() { device.run(); }
