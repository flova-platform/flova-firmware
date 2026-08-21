#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FlovaEsp32.h>

const uint8_t TOUCH_PIN = 4;
const uint8_t LED_PIN = 2;
const uint32_t DEBOUNCE_MS = 60;

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";

FlovaEsp32 device;
WebServer server(80);
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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  led.onWrite(writeLed);
  led.persist(flova::PersistencePolicy::Persistent);
  led.offline(flova::OfflinePolicy::KeepLatest);
  touch.offline(flova::OfflinePolicy::KeepLatest);
  device.attachProvisioning(server);
  server.begin();
  if (!device.begin()) Serial.println("[flova] startup failed");
}

void loop() {
  server.handleClient();
  device.run();
  pollTouch();
  yield();
}
