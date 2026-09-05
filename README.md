<div align="center">
  <h1>FlovaSDK</h1>
  <p><strong>Build connected ESP32 and ESP8266 devices with Flova.</strong></p>
  <p>
    <a href="README_FA.md">فارسی</a> ·
    <a href="https://docs.flova.ir">Documentation</a> ·
    <a href="examples">Examples</a> ·
    <a href="https://github.com/flova-platform/flova-firmware/releases">Releases</a>
  </p>
  <p>
    <a href="https://github.com/flova-platform/flova-firmware/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/flova-platform/flova-firmware?sort=semver&amp;style=flat-square"></a>
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

ESP32:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = flova-platform/FlovaSDK@^0.2.0
```

ESP8266 requires the SDK's bounded BearSSL setup:

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
build_flags = -DPIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED
lib_deps = flova-platform/FlovaSDK@^0.2.0
extra_scripts = pre:$PROJECT_LIBDEPS_DIR/${PIOENV}/FlovaSDK/scripts/patch_esp8266_bearssl_nonblocking.py
```

### Arduino IDE

Arduino Library Manager currently supports ESP32:

1. Open **Tools → Manage Libraries**.
2. Search for **FlovaSDK**.
3. Select **Install**.

Then include the ESP32 entry point:

```cpp
#include <FlovaEsp32.h>
```

Use PlatformIO for ESP8266 projects.

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

## Links

- [Documentation](https://docs.flova.ir)
- [PlatformIO Registry](https://registry.platformio.org/libraries/flova-platform/FlovaSDK)
- [Examples](examples)
- [Releases](https://github.com/flova-platform/flova-firmware/releases)
- [Report an issue](https://github.com/flova-platform/flova-firmware/issues)

## License

FlovaSDK is available under the [MIT License](LICENSE).
