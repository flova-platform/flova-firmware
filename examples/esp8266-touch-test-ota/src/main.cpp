#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FlovaEsp8266.h>

const uint8_t TOUCH_PIN = D1;
const uint8_t LED_PIN = D2;
const uint32_t DEBOUNCE_MS = 60;

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";

FlovaEsp8266 device;
ESP8266WebServer server(80);
flova::Datastream<bool> led = device.datastream<bool>("LED");
flova::Datastream<bool> touch = device.datastream<bool>("TOUCH_SENSOR");

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
  touch.report(touchState, flova::Origin::PhysicalInput);
  if (touchState) led.write(!led.hasValue() || !led.value());
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  led.onWrite(writeLed);
  led.offline(flova::OfflinePolicy::KeepLatest);
  touch.offline(flova::OfflinePolicy::KeepLatest);

  device.setFirmwareTarget("esp8266-touch-test-ota");
  device.enableOta(true);
  device.setOtaProfile(FlovaOtaStrategy::Ab, "esp8266-staged-copy", false);
  device.setRestartHandler(scheduleRestart);

  device.attachProvisioning(server);
  server.begin();
  if (!device.begin()) Serial.println("[flova] startup failed");
}

void loop() {
  server.handleClient();
  device.run();
  if (restartRequestedAt && millis() - restartRequestedAt >= 1000UL) {
    restartRequestedAt = 0;
    ESP.restart();
  }
  pollTouch();
  yield();
}
