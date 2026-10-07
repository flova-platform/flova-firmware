#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <FlovaMqtt.h>
#include <FlovaTlsRoots.h>
#include <time.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* DEVICE_ID = "11111111-1111-1111-1111-111111111111";
const char* DEVICE_SECRET = "replace-with-device-secret";
const uint8_t RELAY_PIN = 2;
BearSSL::WiFiClientSecure socket;
BearSSL::X509List roots(FLOVA_TLS_ROOT_CERTS);
FlovaMqtt client(socket);
uint32_t lastReport = 0;

void onMessage(void*, const char* path, JsonObjectConst payload) {
  if (!strcmp(path, "datastreams/relay") && payload["value"].is<bool>()) {
    bool value = payload["value"].as<bool>();
    digitalWrite(RELAY_PIN, value ? HIGH : LOW);
    client.report("relay", value);
  } else if (!strcmp(path, "config")) {
    Serial.println("[flova] configuration received");
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  socket.setTrustAnchors(&roots);
  // Keep the full handshake RX profile until MQTT TLS-record validation is
  // performed on the target; HAProxy's application record cap is separate.
  socket.setBufferSizes(16384, 2048);
  socket.setTimeout(3000);
  client.subscribe("relay");
  client.onMessage(onMessage);
  if (!client.begin(DEVICE_ID, DEVICE_SECRET)) Serial.println("[flova] MQTT setup failed");
}

void loop() {
  client.run(WiFi.status() == WL_CONNECTED && time(nullptr) > 1700000000);
  uint32_t now = millis();
  if (client.connected() && static_cast<uint32_t>(now - lastReport) >= 30000) {
    lastReport = now;
    client.report("temperature", 23.4);
    StaticJsonDocument<128> metadata;
    metadata["firmware_version"] = "custom-mqtt-example";
    client.heartbeat(metadata.as<JsonObjectConst>());
  }
  yield();
}
