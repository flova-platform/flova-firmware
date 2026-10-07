#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FlovaMqtt.h>
#include <FlovaTlsRoots.h>
#include <time.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
// Supply each device's UUID and secret from your secure provisioning/storage.
const char* DEVICE_ID = "11111111-1111-1111-1111-111111111111";
const char* DEVICE_SECRET = "replace-with-device-secret";
const uint8_t RELAY_PIN = 2;
WiFiClientSecure socket;
FlovaMqtt client(socket);
uint32_t lastReport = 0;

void onMessage(void*, const char* path, JsonObjectConst payload) {
  if (!strcmp(path, "datastreams/relay") && payload["value"].is<bool>()) {
    bool value = payload["value"].as<bool>();
    digitalWrite(RELAY_PIN, value ? HIGH : LOW);
    client.report("relay", value);
  } else if (!strcmp(path, "config")) {
    // Apply and persist the configuration before acknowledging its generation
    // and checksum. This example does not auto-ack unimplemented settings.
    Serial.println("[flova] configuration received");
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  socket.setCACert(FLOVA_TLS_ROOT_CERTS);
  socket.setTimeout(3000);
  client.subscribe("relay");
  client.onMessage(onMessage);
  if (!client.begin(DEVICE_ID, DEVICE_SECRET)) Serial.println("[flova] MQTT setup failed");
}

void loop() {
  // A valid UTC clock is required for certificate verification.
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
