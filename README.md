# Flova SDK

The Flova SDK connects ESP32 and ESP8266 Arduino applications to Flova through
the bounded local-first device runtime.

Install `FlovaSDK` from Arduino Library Manager for ESP32, or from the PlatformIO
Registry for ESP32 and ESP8266.

```cpp
#include <WiFi.h>
#include <FlovaEsp32.h>

FlovaEsp32 flovaDevice;
auto relay = flovaDevice.datastream<bool>("relay");

void setup() {
  WiFi.begin("your-wifi", "your-password");
  pinMode(2, OUTPUT);
  relay.onWrite([](bool enabled) { digitalWrite(2, enabled ? HIGH : LOW); });
  flovaDevice.begin();
}

void loop() { flovaDevice.run(); }
```

Use `<FlovaUniversalEsp32.h>` when Flova owns provisioning, networking,
hardware mappings, OTA, and restart policy. PlatformIO ESP8266 applications
use `<FlovaEsp8266.h>` and the configuration in
`extras/platformio/esp8266/platformio.ini`; its cooperative BearSSL transport
needs the packaged pinned-framework preparation step.

The canonical source is maintained in
[`flova-platform/flova-firmware`](https://github.com/flova-platform/flova-firmware).
