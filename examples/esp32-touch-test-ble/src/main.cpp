#include <Arduino.h>
#include <FlovaEsp32Ble.h>

const uint8_t TOUCH_PIN = 4;
const uint8_t LED_PIN = 2;
const uint32_t DEBOUNCE_MS = 60;

FlovaEsp32Ble device;
flova::Datastream<bool> led = device.datastream<bool>("LED");
flova::Datastream<bool> touch = device.datastream<bool>("TOUCH_SENSOR");

bool touchState = false;
bool lastRawTouch = false;
uint32_t rawTouchChangedAt = 0;

void writeLed(bool enabled) { digitalWrite(LED_PIN, enabled ? HIGH : LOW); }

void pollTouch() {
  const uint32_t now = millis();
  const bool rawTouch = digitalRead(TOUCH_PIN) == HIGH;

  if (rawTouch != lastRawTouch) {
    lastRawTouch = rawTouch;
    rawTouchChangedAt = now;
  }

  if (rawTouch == touchState || now - rawTouchChangedAt < DEBOUNCE_MS)
    return;

  touchState = rawTouch;
  touch.report(touchState, flova::Origin::PhysicalInput);
  if (touchState) led.write(!led.hasValue() || !led.value());
}

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  led.onWrite(writeLed);
  led.persist(flova::PersistencePolicy::Persistent);
  led.offline(flova::OfflinePolicy::KeepLatest);
  touch.offline(flova::OfflinePolicy::KeepLatest);
  if (!device.begin()) Serial.println("[flova] BLE startup failed");
}

void loop() {
  device.run();
  pollTouch();
  yield();
}
