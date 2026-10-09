#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FlovaSDK.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

#include "credentials.h"

namespace {

// Application-owned Wi-Fi for the temporary bench device.
const char* const WIFI_SSID = "home_wifi";
const char* const WIFI_PASSWORD = "home_wifi_123";

  // Seven-pin ST7789 wiring with CS handled internally by the panel:
// TFT SCL=D5, SDA=D7, RES=D2, DC=D1, BLK=3V3. Leave D0 unconnected.
const int8_t TFT_CS = -1;
const uint8_t TFT_DC = D1;
const uint8_t TFT_RST = D2;
const uint8_t MOISTURE_PIN = A0;
const uint8_t DRY_INDICATOR_PIN = LED_BUILTIN;

const uint16_t SCREEN_WIDTH = 240;
const uint16_t SCREEN_HEIGHT = 240;
const uint8_t SENSOR_SAMPLES = 8;
const uint32_t SENSOR_INTERVAL_MS = 250;
const uint32_t DISPLAY_INTERVAL_MS = 500;
const uint32_t REPORT_INTERVAL_MS = 10000;
const uint32_t MIN_REPORT_INTERVAL_MS = 1000;
const uint32_t MAX_REPORT_INTERVAL_MS = 60000;
const uint32_t MQTT_TLS_TIMEOUT_MS = 750;
const uint16_t MQTT_PACKET_TIMEOUT_SECONDS = 1;
const uint32_t NTP_VALID_EPOCH = 1700000000;

Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);
BearSSL::WiFiClientSecure mqttSocket;
BearSSL::X509List mqttRoots(FLOVA_TLS_ROOT_CERTS);
PubSubClient mqtt(mqttSocket);
FlovaEsp8266 client(mqtt);

// These names match the template parameters. The calibration values come from
// the current bench sensor: wet in water is about 270-280 and dry in air is
// about 608 or higher.
auto dryThreshold = FLOVA_SETTING(client, float, "dry_threshold", 35.0f);
auto dryCalibration = FLOVA_SETTING(client, int64_t, "dry_adc", 608);
auto wetCalibration = FLOVA_SETTING(client, int64_t, "wet_adc", 280);

// Production test10 currently declares this exact key as a read-only double.
auto moisture = FLOVA_DATASTREAM(client, double, "MOISTURE");

struct MoistureReading {
  uint16_t raw;
  float percent;
  bool dry;
};

MoistureReading latest = {0, 0.0f, true};
uint16_t minimumRaw = 1023;
uint16_t maximumRaw = 0;
bool hasMoistureReading = false;
uint32_t lastSensorMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastReportMs = 0;

int32_t boundedCalibration(const flova::Setting<int64_t>& setting,
                           int32_t fallback) {
  if (!setting.valid() || !setting.hasValue()) return fallback;
  const int64_t value = setting.value();
  return static_cast<int32_t>(constrain(value, 0LL, 1023LL));
}

float boundedThreshold() {
  if (!dryThreshold.valid() || !dryThreshold.hasValue()) return 35.0f;
  return constrain(dryThreshold.value(), 0.0f, 100.0f);
}

uint32_t boundedReportInterval() {
  // The production template has no interval parameter yet. Keep reporting
  // frequent enough for a useful screen without adding another parameter.
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

void updateIndicator(bool dry) {
  // The onboard ESP8266 LED is active-low. This is only a dry-state indicator;
  // connect a relay through a proper driver before controlling a pump.
  digitalWrite(DRY_INDICATOR_PIN, dry ? LOW : HIGH);
}

void sampleMoisture() {
  const int32_t dryAdc = boundedCalibration(dryCalibration, 608);
  const int32_t wetAdc = boundedCalibration(wetCalibration, 280);
  latest.raw = readMoistureRaw();
  latest.percent = moisturePercent(latest.raw, dryAdc, wetAdc);
  latest.dry = latest.percent < boundedThreshold();
  if (!hasMoistureReading) {
    minimumRaw = latest.raw;
    maximumRaw = latest.raw;
    hasMoistureReading = true;
  } else {
    minimumRaw = min(minimumRaw, latest.raw);
    maximumRaw = max(maximumRaw, latest.raw);
  }
  updateIndicator(latest.dry);
}

uint16_t connectionColor() {
  if (client.connected()) return ST77XX_GREEN;
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED || status == WL_IDLE_STATUS)
    return ST77XX_ORANGE;
  return ST77XX_RED;
}

void drawStaticDisplay() {
  display.fillScreen(ST77XX_BLACK);
}

void drawDisplay() {
  char value[40];
  display.setTextWrap(false);
  static bool rendered = false;
  static float lastPercent = -1.0f;
  static uint16_t lastMinimumRaw = 0;
  static uint16_t lastRaw = 0;
  static uint16_t lastMaximumRaw = 0;

  if (!rendered || latest.percent != lastPercent) {
    display.fillRect(20, 70, 205, 55, ST77XX_BLACK);
    display.setTextSize(5);
    display.setTextColor(ST77XX_GREEN);
    snprintf(value, sizeof(value), "%6.1f%%", latest.percent);
    display.setCursor(27, 78);
    display.print(value);
    lastPercent = latest.percent;
  }

  if (!rendered || minimumRaw != lastMinimumRaw || latest.raw != lastRaw ||
      maximumRaw != lastMaximumRaw) {
    display.fillRect(0, 212, SCREEN_WIDTH, 20, ST77XX_BLACK);
    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE);
    snprintf(value, sizeof(value), "MIN:%4u RAW:%4u MAX:%4u", minimumRaw,
             latest.raw, maximumRaw);
    display.setCursor(8, 220);
    display.print(value);
    lastMinimumRaw = minimumRaw;
    lastRaw = latest.raw;
    lastMaximumRaw = maximumRaw;
  }

  static uint16_t lastConnectionColor = 0;
  const uint16_t currentConnectionColor = connectionColor();
  if (currentConnectionColor != lastConnectionColor) {
    display.fillCircle(224, 18, 7, currentConnectionColor);
    lastConnectionColor = currentConnectionColor;
  }
  rendered = true;
  yield();
}

bool networkReady() {
  return WiFi.status() == WL_CONNECTED && time(nullptr) > NTP_VALID_EPOCH;
}

void reportMoisture(uint32_t now) {
  if (!client.connected() || now - lastReportMs < boundedReportInterval())
    return;
  lastReportMs = now;
  FLOVA_REPORT(moisture, static_cast<double>(latest.percent),
               flova::Origin::PhysicalInput);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("[boot] reset=%s\n", ESP.getResetReason().c_str());

  pinMode(DRY_INDICATOR_PIN, OUTPUT);
  updateIndicator(false);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "time.cloudflare.com", "time.google.com", "pool.ntp.org");

  mqttSocket.setTrustAnchors(&mqttRoots);
  mqttSocket.setBufferSizes(16384, 2048);
  mqttSocket.setTimeout(MQTT_TLS_TIMEOUT_MS);
  if (!client.begin(FLOVA_DEVICE_ID, FLOVA_DEVICE_SECRET)) {
    Serial.println(F("[flova] MQTT setup failed"));
  } else {
    // PubSubClient otherwise waits three seconds for a failed CONNACK, which
    // would pause the local sensor/display loop during reconnect attempts.
    mqtt.setSocketTimeout(MQTT_PACKET_TIMEOUT_SECONDS);
  }

  // Start networking before the panel workload so the ESP8266 handles the
  // Wi-Fi association surge before the display and its backlight are active.
  SPI.begin();
  display.setSPISpeed(8000000);
  display.init(SCREEN_WIDTH, SCREEN_HEIGHT, SPI_MODE3);
  display.setRotation(2);
  display.invertDisplay(true);
  drawStaticDisplay();
  sampleMoisture();
  drawDisplay();
  Serial.println(F("[display] initialized"));
}

void loop() {
  const uint32_t now = millis();
  // Keep local sensing and rendering ahead of transport work. MQTT/TLS retries
  // may block briefly, but the panel always has a current local reading.
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    sampleMoisture();
  }

  if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = now;
    drawDisplay();
  }

  // The MQTT adapter owns reconnect backoff. Keeping it gated until Wi-Fi and
  // NTP are ready avoids a blocking TLS attempt during ESP8266 boot/reset.
  client.run(networkReady());
  reportMoisture(now);
  yield();
  delay(10);
}
