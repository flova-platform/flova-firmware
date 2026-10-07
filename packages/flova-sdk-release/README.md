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
no framework patch is needed. The packaged PlatformIO profile selects the
`16KB cache + 48KB IRAM and 2nd Heap (shared)` layout required for Flova's
full-record BearSSL buffers.

For Arduino IDE, select the same profile before compiling:

1. Open **Tools → MMU**.
2. Select **16KB cache + 48KB IRAM and 2nd Heap (shared)**.

TLS connect operations can pause the application loop for seconds. Link and OTA
require sufficient contiguous heap for full TLS records; allocation failures
are reported without erasing provisioning.

For a custom application, include `<FlovaSDK.h>`. It selects the right board
facade and gives you the shared `FLOVA_*` helpers.

See the [main README](../../README.md#quick-start) for the smallest working
example.

To use MQTT, create your `PubSubClient` and pass it to the same device
constructor. MQTT uses `mqtt.flova.ir:8883`, the device UUID as username, and
the device secret as password. It uses the same datastream helpers as Flova
Link. See the [main MQTT example](../../README.md#mqtt-transport) and the
[complete example](../../examples/custom-mqtt).

The canonical source is maintained in
[`flova-platform/flova-firmware`](https://github.com/flova-platform/flova-firmware).
