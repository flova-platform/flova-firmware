#include <Arduino.h>
#include <FlovaEsp32Ble.h>

const uint8_t TOUCH_PIN = 4;
const uint8_t LED_PIN = 2;
const uint32_t DEBOUNCE_MS = 60;

FlovaEsp32Ble device;
auto led = FLOVA_DATASTREAM(device, bool, "LED");
auto touch = FLOVA_DATASTREAM(device, bool, "TOUCH_SENSOR");

bool touchState = false;
bool lastRawTouch = false;
uint32_t rawTouchChangedAt = 0;
uint32_t restartRequestedAt = 0;

void scheduleRestart(void*, FlovaRestartReason) {
  const uint32_t now = millis();
  restartRequestedAt = now ? now : 1;
}

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
  FLOVA_REPORT(touch, touchState, flova::Origin::PhysicalInput);
  if (touchState) FLOVA_WRITE(led, !FLOVA_HAS_VALUE(led) || !FLOVA_READ(led));
}

void setup() {
  Serial.begin(115200);
  Serial.printf("[flova] OTA test build %s\n", FLOVA_FIRMWARE_VERSION);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  device.enableOta(true);
  device.setOtaProfile(FlovaOtaStrategy::Ab, "esp32-ab-4m-v1", true);
  device.setRestartHandler(scheduleRestart);
  FLOVA_ON_WRITE(led, writeLed);
  led.persist(flova::PersistencePolicy::Persistent);
  led.offline(flova::OfflinePolicy::KeepLatest);
  touch.offline(flova::OfflinePolicy::KeepLatest);
  if (!device.begin()) Serial.println("[flova] BLE startup failed");
}

void loop() {
  device.run();
  if (restartRequestedAt && millis() - restartRequestedAt >= 1000UL) {
    restartRequestedAt = 0;
    ESP.restart();
  }
  pollTouch();
  yield();
}
