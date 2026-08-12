#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FlovaEsp32.h>

namespace {
// This demonstrates integration into an existing application that already
// owns Wi-Fi and an HTTP server. It is intentionally different from
// FlovaUniversalEsp32: the SDK will not create a SoftAP or replace this server.
const uint8_t RELAY_PIN = 2;
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";

FlovaEsp32 client;
WebServer server(80);
// Datastream keys are stable developer/API names. Engine supplies the compact
// runtime ID after the device connects and binds its declared keys.
flova::Datastream<bool> relay = client.datastream<bool>("LED");
}

void setup() {
  Serial.begin(115200);

  // The application chooses how and where it joins Wi-Fi. A BLE, Ethernet,
  // cellular, or factory flow could provide the same ProvisioningHandoff
  // directly instead of using the helper routes below.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Dashboard, user, automation, and schedule writes all use this one safe
  // actuator path. The handler runs from client.run().
  relay.onWrite([](bool enabled) {
    digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
  });
  // attachProvisioning() only registers /status and /provision on the server
  // supplied by the application. It does not start, stop, or service it.
  client.attachProvisioning(server);
  server.begin();

  // A new device waits in AwaitingProvisioning until its selected channel
  // supplies a link URL/token handoff. Existing devices restore identity.
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // The application remains responsible for servicing its own web server.
  server.handleClient();

  // Flova processes Device Link, applies remote commands, sends state, and
  // performs reconnect work from this cooperative loop.
  client.run();
  yield();
}
