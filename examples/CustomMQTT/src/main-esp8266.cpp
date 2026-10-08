#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <PubSubClient.h>
#include <FlovaSDK.h>
#include <time.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* DEVICE_ID = "11111111-1111-1111-1111-111111111111";
const char* DEVICE_SECRET = "replace-with-device-secret";
const uint8_t RELAY_PIN = 2;

BearSSL::WiFiClientSecure socket;
BearSSL::X509List roots(FLOVA_TLS_ROOT_CERTS);
PubSubClient mqtt(socket);
FlovaEsp8266 device(mqtt);
auto relay = FLOVA_DATASTREAM(device, bool, "relay");
auto temperature = FLOVA_DATASTREAM(device, float, "temperature");
uint32_t lastReport = 0;

void writeRelay(bool enabled) {
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  socket.setTrustAnchors(&roots);
  socket.setBufferSizes(16384, 2048);
  socket.setTimeout(3000);
  FLOVA_ON_WRITE(relay, writeRelay);
  if (!device.begin(DEVICE_ID, DEVICE_SECRET))
    Serial.println("[flova] MQTT setup failed");
}

void loop() {
  device.run(WiFi.status() == WL_CONNECTED && time(nullptr) > 1700000000);
  const bool connected = device.connected();
  if (connected && static_cast<uint32_t>(millis() - lastReport) >= 30000) {
    lastReport = millis();
    FLOVA_REPORT(temperature, 23.4f);
  }
  yield();
}
