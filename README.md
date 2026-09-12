<div align="center">
  <h1>FlovaSDK</h1>
  <p><strong>Build connected ESP32 and ESP8266 devices with Flova.</strong></p>
  <p>
    <a href="README_FA.md">فارسی</a> ·
    <a href="https://docs.flova.ir">Documentation</a> ·
    <a href="examples">Examples</a> ·
    <a href="https://github.com/flova-platform/flova-firmware/tags">SDK versions</a>
  </p>
  <p>
    <a href="https://github.com/flova-platform/flova-firmware/tags"><img alt="SDK version" src="https://img.shields.io/github/v/tag/flova-platform/flova-firmware?filter=v*&amp;sort=semver&amp;style=flat-square&amp;label=SDK"></a>
    <a href="https://registry.platformio.org/libraries/flova-platform/FlovaSDK"><img alt="PlatformIO Registry" src="https://badges.registry.platformio.org/packages/flova-platform/library/FlovaSDK.svg"></a>
    <a href="https://github.com/arduino/library-registry/pull/9043"><img alt="Arduino Library Manager" src="https://img.shields.io/badge/Arduino%20Library%20Manager-FlovaSDK-00878F?style=flat-square&amp;logo=arduino&amp;logoColor=white"></a>
    <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/github/license/flova-platform/flova-firmware?style=flat-square"></a>
  </p>
</div>

FlovaSDK is the official embedded C++ SDK for connecting devices to the Flova
platform. It provides ready-to-use integrations for ESP32 and ESP8266, with a
portable C++11 core for custom hardware.

> For provisioning, datastreams, device configuration, OTA, and complete API
> guides, visit **[docs.flova.ir](https://docs.flova.ir)**.

## Install

### PlatformIO

For an existing ESP32 PlatformIO project, add:

```ini
lib_deps = flova-platform/FlovaSDK@^0.3.4
```

For ESP8266, use the packaged `extras/platformio/esp8266/platformio.ini`.
It includes the required shared IRAM heap profile:

```ini
lib_deps = flova-platform/FlovaSDK@^0.3.4
build_flags =
  -DPIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED
```

### Arduino IDE

FlovaSDK 0.3.4 supports ESP32 and ESP8266 in Arduino IDE:

1. Open **Tools → Manage Libraries**.
2. Search for **FlovaSDK**.
3. Select **Install**.

Then include the ESP32 entry point:

```cpp
#include <FlovaEsp32.h>
```

For ESP8266, include `<FlovaEsp8266.h>` and select
**Tools → MMU → 16KB cache + 48KB IRAM and 2nd Heap (shared)** before compiling.

## Quick start

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <FlovaEsp32.h>

FlovaEsp32 flovaDevice;
auto relay = flovaDevice.datastream<bool>("relay");

void setup() {
  WiFi.begin("your-wifi", "your-password");
  pinMode(2, OUTPUT);

  relay.onWrite([](bool enabled) {
    digitalWrite(2, enabled ? HIGH : LOW);
  });

  flovaDevice.begin();
}

void loop() {
  flovaDevice.run();
}
```

For ESP8266, use `<ESP8266WiFi.h>` and `<FlovaEsp8266.h>`. See the
[examples](examples) or follow the [documentation](https://docs.flova.ir) for
provisioning and production setup.

## Universal firmware from the SDK

Build and upload the universal composition from your own project. The selected
toolchain creates the complete board image, including the bootloader and
partition table; no prebuilt firmware download is required.

For ESP32, install `FlovaSDK` from Arduino Library Manager or the PlatformIO
Registry and include `<FlovaUniversalEsp32.h>`:

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

Use an ESP32 board. Arduino IDE or PlatformIO will upload the complete image.

For ESP8266, use `<FlovaUniversalEsp8266.h>` with Arduino IDE or PlatformIO.
See [transport behavior](packages/TRANSPORT.md) for ESP8266 timing and memory requirements.

## Links

- [Documentation](https://docs.flova.ir)
- [PlatformIO Registry](https://registry.platformio.org/libraries/flova-platform/FlovaSDK)
- [Examples](examples)
- [SDK versions](https://github.com/flova-platform/flova-firmware/tags)
- [Report an issue](https://github.com/flova-platform/flova-firmware/issues)

## License

FlovaSDK is available under the [MIT License](LICENSE).
