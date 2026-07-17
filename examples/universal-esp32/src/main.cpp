#include <Arduino.h>
#include <FlovaEsp32.h>

FlovaEsp32 device;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp32 boot");
  device.begin();
}

void loop() {
  device.loop();
}
