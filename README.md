# Flova SDK

The Flova SDK connects ESP32 and ESP8266 applications to Flova through the
bounded local-first device runtime. Build and upload from your own project;
the board toolchain creates the complete flash image for the selected board.

Install `FlovaSDK` from the PlatformIO Registry for ESP32 and ESP8266. Arduino
IDE users can install it from Library Manager for either target.

## ESP32 universal firmware

In Arduino IDE, select an ESP32 board, install `FlovaSDK`, and use:

```cpp
#include <Arduino.h>
#include <FlovaUniversalEsp32.h>

FlovaUniversalEsp32 device;

void setup() {
  Serial.begin(115200);
  device.begin();
}

void loop() { device.run(); }
```

`FlovaUniversalEsp32` owns provisioning, networking, hardware mappings, OTA,
and restart policy. Arduino IDE upload includes the bootloader and partition
table automatically.

## ESP8266 universal firmware

Use Arduino IDE or the packaged `extras/platformio/esp8266/platformio.ini`
with `<FlovaUniversalEsp8266.h>`. Stock Arduino ESP8266 core 3.1.2 is supported;
no framework patch is needed. TLS connect operations can pause the application
loop for seconds. Link and OTA require sufficient contiguous heap for full TLS
records; allocation failures are reported without erasing provisioning.

For custom applications, use `<FlovaEsp32.h>` or `<FlovaEsp8266.h>` instead of
the universal composition and own provisioning, networking, and hardware
policy in your application.

The canonical source is maintained in
[`flova-platform/flova-firmware`](https://github.com/flova-platform/flova-firmware).
