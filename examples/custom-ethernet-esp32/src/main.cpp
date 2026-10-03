#include <Arduino.h>

// Fill these per device. The factory token comes from Console; it is not the
// activation QR code scanned by the end user.
#define FLOVA_DEVICE_TEMPLATE_ID "template-id"
#define FLOVA_DEVICE_NAME "Ethernet relay"
#define FLOVA_DEVICE_PROVISION_TOKEN "factory-token"
#define FLOVA_DEVICE_LINK_URL "wss://your-engine.example/flova-link"

#include <FlovaEsp32Ethernet.h>

FlovaEsp32Ethernet client;
auto enabled = FLOVA_DATASTREAM(client, bool, "enabled");

flova::WriteResult setEnabled(bool value) {
  // Replace this with the application's relay or output write.
  digitalWrite(2, value ? HIGH : LOW);
  return flova::WriteResult::accept();
}

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);
  FLOVA_ON_WRITE(enabled, setEnabled);
  if (!client.begin()) Serial.println("Ethernet startup failed");
}

void loop() {
  client.run();
  delay(1);
}
