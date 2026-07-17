#include <Arduino.h>

constexpr uint8_t TOUCH_PIN = D0;
constexpr uint8_t LED_PIN = D1;

bool ledOn = false;
bool lastTouch = false;
unsigned long lastToggleAt = 0;

void setup() {
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  const bool touch = digitalRead(TOUCH_PIN) == HIGH;

  if (touch && !lastTouch && millis() - lastToggleAt > 100) {
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    lastToggleAt = millis();
  }

  lastTouch = touch;
}
