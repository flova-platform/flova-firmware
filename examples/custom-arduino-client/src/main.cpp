#include <Arduino.h>
#include <WiFi.h>
#include <FlovaEsp32.h>

namespace {
// This is an existing application example. Flova does not own these Wi-Fi
// credentials, the GPIO pin, the application's web server, or the main loop.
// The same binary can be copied to multiple devices; the device identity and
// secret arrive later through the selected provisioning channel.
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const uint8_t RELAY_PIN = 2;
struct RelayContext { uint8_t pin; } relayContext = {RELAY_PIN};

flova::WriteResult writeRelay(void* context, bool value) {
  // User dashboard actions, schedules, and Engine automations all arrive here
  // when they write this datastream. The callback runs from client.run(), not
  // from a socket callback, so it is safe to update application hardware.
  RelayContext* relay = static_cast<RelayContext*>(context);
  digitalWrite(relay->pin, value ? HIGH : LOW);
  return flova::WriteResult::accept();
}

FlovaEsp32 client;
// "LED" is the developer/API key. Engine resolves it to a compact numeric
// runtime ID during binding; the string is not sent with every update.
flova::Datastream<bool> relay = client.datastream<bool>("LED");
bool lastReady = false;
}

void setup() {
  Serial.begin(115200);

  // Wi-Fi remains application-owned. Flova observes connectivity and waits
  // for it when it needs to open Device Link.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // The application chooses the safe hardware boot state.
  pinMode(relayContext.pin, OUTPUT);
  digitalWrite(relayContext.pin, LOW);

  // onWrite() handles remote writes. Local logic can use relay.write(value),
  // while sensors or externally changed hardware should use relay.report(value).
  relay.onWrite(writeRelay, &relayContext);

  // begin() restores Flova-private state or waits for provisioning. It does
  // not start a SoftAP, replace a server, change Wi-Fi mode, or reboot.
  if (!client.begin()) Serial.println("[flova] client startup failed");
}

void loop() {
  // Keep this call frequent. It polls Link, applies queued remote commands,
  // sends dirty state, and runs Flova callbacks on this loop.
  client.run();
  const bool ready = client.ready();
  if (ready != lastReady) {
    Serial.println(ready ? "[flova] link ready" : "[flova] link offline");
    lastReady = ready;
  }

  // Existing application work continues here. Avoid long blocking operations
  // so remote commands remain responsive.
  yield();
}
