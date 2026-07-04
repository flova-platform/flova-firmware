#include <Arduino.h>
#include <FlovaEsp32.h>

FlovaEsp32 flova;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("[flova] esp32 boot");
  flova.begin();
}

void loop() {
  flova.loop();
}
