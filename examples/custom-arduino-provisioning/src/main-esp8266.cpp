#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FlovaEsp8266.h>

namespace {
// Existing-project integration: this sketch owns Wi-Fi, the HTTP server, the
// GPIO, and reboot policy. Flova adds bounded runtime services and optional
// provisioning routes without taking over the application.
const uint8_t RELAY_PIN = LED_BUILTIN;
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
FlovaEsp8266 client;
ESP8266WebServer server(80);
// The key is used for SDK declarations and Engine configuration. Device Link
// later uses the server-assigned numeric ID, not this string on every frame.
flova::Datastream<bool> relay = client.datastream<bool>("LED");
}

void setup() {
  Serial.begin(115200);

  // The passive SDK never takes over this network connection. Other channels
  // can call provision() with the same channel-neutral Link URL/token handoff.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // LED_BUILTIN is active-low.

  // Dashboard, user, schedule, and automation writes all reach this callback.
  relay.onWrite([](bool enabled) {
    digitalWrite(RELAY_PIN, enabled ? LOW : HIGH);
  });
  // These routes are attached to the application's existing server. The SDK
  // does not start a SoftAP or silently stop this server after provisioning.
  client.attachProvisioning(server);
  server.begin();

  // A fresh device waits for a ProvisioningHandoff; a configured device
  // restores its private identity and continues toward runtime.
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // Service the application-owned server, then process queued protocol work
  // and invoke hardware handlers from the loop.
  server.handleClient();
  client.run();
  yield();
}
