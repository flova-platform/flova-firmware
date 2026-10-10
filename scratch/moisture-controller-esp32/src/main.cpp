#include <Arduino.h>
#include <FlovaSDK.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "credentials.h"

namespace {

const char* const WIFI_SSID = FLOVA_WIFI_SSID;
const char* const WIFI_PASSWORD = FLOVA_WIFI_PASSWORD;

// GPIO32 is ADC1_CH4 on the classic ESP32 DevKit and remains usable while
// Wi-Fi is active. Power the sensor from 3V3 so its analog output is safe.
const uint8_t MOISTURE_PIN = 32;
// The classic ESP32 DevKit's built-in LED is active-high on GPIO2. The
// PlatformIO esp32dev board definition does not provide LED_BUILTIN.
const uint8_t LED_PIN = 2;
const uint8_t SENSOR_SAMPLES = 8;
const uint32_t SENSOR_INTERVAL_MS = 250;
const uint32_t LOG_INTERVAL_MS = 1000;
const uint32_t REPORT_INTERVAL_MS = 10000;
const uint32_t MIN_REPORT_INTERVAL_MS = 1000;
const uint32_t MAX_REPORT_INTERVAL_MS = 60000;
const uint32_t NTP_VALID_EPOCH = 1700000000;

WiFiClientSecure mqttSocket;
PubSubClient mqtt(mqttSocket);
FlovaEsp32 client(mqtt);

auto dryThreshold = FLOVA_SETTING(client, float, "dry_threshold", 35.0f);
auto dryCalibration = FLOVA_SETTING(client, int64_t, "dry_adc", 3000);
auto wetCalibration = FLOVA_SETTING(client, int64_t, "wet_adc", 1200);
auto moisture = FLOVA_DATASTREAM(client, double, "MOISTURE");
auto led = FLOVA_DATASTREAM(client, bool, "led");

uint16_t latestRaw = 0;
float latestPercent = 0.0f;
uint32_t lastSensorMs = 0;
uint32_t lastLogMs = 0;
uint32_t lastReportMs = 0;

int32_t boundedCalibration(const flova::Setting<int64_t>& setting,
                           int32_t fallback) {
  if (!setting.valid() || !setting.hasValue()) return fallback;
  return static_cast<int32_t>(constrain(setting.value(), 0LL, 4095LL));
}

float boundedThreshold() {
  if (!dryThreshold.valid() || !dryThreshold.hasValue()) return 35.0f;
  return constrain(dryThreshold.value(), 0.0f, 100.0f);
}

uint32_t boundedReportInterval() {
  return constrain(REPORT_INTERVAL_MS, MIN_REPORT_INTERVAL_MS,
                   MAX_REPORT_INTERVAL_MS);
}

uint16_t readMoistureRaw() {
  uint32_t total = 0;
  for (uint8_t sample = 0; sample < SENSOR_SAMPLES; ++sample) {
    total += analogRead(MOISTURE_PIN);
    yield();
  }
  return static_cast<uint16_t>(total / SENSOR_SAMPLES);
}

float moisturePercent(uint16_t raw, int32_t dryAdc, int32_t wetAdc) {
  if (dryAdc == wetAdc) return 0.0f;
  const float percent =
      (static_cast<float>(dryAdc) - raw) * 100.0f /
      static_cast<float>(dryAdc - wetAdc);
  return constrain(percent, 0.0f, 100.0f);
}

flova::WriteResult writeLed(bool enabled) {
  digitalWrite(LED_PIN, enabled ? HIGH : LOW);
  return flova::WriteResult::accept();
}

void sampleMoisture() {
  const int32_t dryAdc = boundedCalibration(dryCalibration, 3000);
  const int32_t wetAdc = boundedCalibration(wetCalibration, 1200);
  latestRaw = readMoistureRaw();
  latestPercent = moisturePercent(latestRaw, dryAdc, wetAdc);
}

bool networkReady() {
  return WiFi.status() == WL_CONNECTED && time(nullptr) > NTP_VALID_EPOCH;
}

void reportMoisture(uint32_t now) {
  if (!client.connected() || now - lastReportMs < boundedReportInterval())
    return;
  lastReportMs = now;
  const flova::WriteResult result =
      FLOVA_REPORT(moisture, static_cast<double>(latestPercent),
                   flova::Origin::PhysicalInput);
  Serial.printf("[mqtt] report=%s reason=%s\n",
                result.accepted() ? "accepted" : "rejected",
                result.reason ? result.reason : "none");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "time.cloudflare.com", "time.google.com", "pool.ntp.org");

  analogReadResolution(12);
  analogSetPinAttenuation(MOISTURE_PIN, ADC_11db);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // The SDK publishes the accepted value after this handler returns. That
  // state update keeps the PWA toggle synchronized after the command ack.
  FLOVA_ON_WRITE(led, writeLed);
  led.persist(flova::PersistencePolicy::Persistent);
  led.offline(flova::OfflinePolicy::KeepLatest);

  mqttSocket.setCACert(FLOVA_TLS_ROOT_CERTS);
  mqttSocket.setTimeout(3000);
  if (!client.begin(FLOVA_DEVICE_ID, FLOVA_DEVICE_SECRET))
    Serial.println(F("[flova] MQTT setup failed"));
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    sampleMoisture();
  }

  if (now - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = now;
    Serial.printf("[moisture] raw=%u percent=%.1f wifi=%s ntp=%s mqtt=%s state=%d\n",
                  latestRaw, latestPercent,
                  WiFi.status() == WL_CONNECTED ? "connected" : "offline",
                  time(nullptr) > NTP_VALID_EPOCH ? "ready" : "waiting",
                  client.connected() ? "connected" : "offline", mqtt.state());
  }

  client.run(networkReady());
  reportMoisture(now);
  yield();
  delay(10);
}
