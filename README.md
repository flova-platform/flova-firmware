<div align="center">
  <h1>FlovaSDK</h1>
  <p><strong>Build connected Linux, ESP32, and ESP8266 devices with Flova.</strong></p>
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
platform. It provides ready-to-use integrations for Linux, ESP32, and ESP8266,
with a portable C++11 core for custom hardware.

> For provisioning, datastreams, device configuration, OTA, and complete API
> guides, visit **[docs.flova.ir](https://docs.flova.ir)**.

`FlovaEsp32` and `FlovaEsp8266` use Flova Link by default. If you prefer MQTT,
pass your `PubSubClient` to the same device constructor. Your datastream code
stays the same. See [`examples/custom-mqtt`](examples/custom-mqtt) for the
complete example.

## Install

### PlatformIO

For an existing ESP32 PlatformIO project, add:

```ini
  lib_deps = flova-platform/FlovaSDK@^0.3.11
```

For ESP8266, use the packaged `extras/platformio/esp8266/platformio.ini`.
It provides the tested ESP8266 board and TLS profile for the SDK.

### Arduino IDE

FlovaSDK 0.3.11 supports ESP32 and ESP8266 in Arduino IDE:

1. Open **Tools → Manage Libraries**.
2. Search for **FlovaSDK**.
3. Select **Install**.

Then include the single SDK entry point:

```cpp
#include <FlovaSDK.h>
```

`FlovaSDK.h` selects the ESP32 or ESP8266 facade for the board you selected.
It also exposes the custom datastream helpers and the built-in TLS roots used
by the MQTT adapter.

### Linux and Raspberry Pi

Build and install the native Linux package with CMake. OpenSSL development
headers are required for OTA digest verification:

```sh
sudo apt install cmake libssl-dev
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
```

Linux applications use `find_package(FlovaLinux CONFIG REQUIRED)` and link
`Flova::Linux`. The package provides POSIX clock, durable file storage,
logging, and supervisor-owned OTA staging. The application supplies its
`flova::Link` transport and owns the systemd service.

## Quick start

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <FlovaSDK.h>

FlovaEsp32 device;
auto relay = FLOVA_DATASTREAM(device, bool, "relay");

void setRelay(bool enabled) {
  digitalWrite(2, enabled ? HIGH : LOW);
}

void setup() {
  WiFi.begin("your-wifi", "your-password");
  pinMode(2, OUTPUT);
  FLOVA_ON_WRITE(relay, setRelay);
  device.begin();
}

void loop() { device.run(); }
```

The same `FLOVA_*` helpers work with both transports. Datastream keys must
match the device's template.

## MQTT transport

MQTT is optional. Create your MQTT client, then pass it to the device:

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <FlovaSDK.h>

WiFiClientSecure socket;
PubSubClient mqtt(socket);
FlovaEsp32 device(mqtt);
```

Use `mqtt.flova.ir:8883` with the device UUID and secret. The MQTT adapter
uses the same JSON datastream format and helpers as Flova Link.

See the [custom MQTT example](examples/custom-mqtt) for Wi-Fi, TLS, reporting,
and ESP8266 setup. See the [documentation](https://docs.flova.ir) for
provisioning and production setup.

## Firmware logging

Flova diagnostics are compile-time filtered. Production builds default to
warnings and errors:

```ini
build_flags =
  -DFLOVA_LOGGING_ENABLED=1
  -DFLOVA_LOG_LEVEL=FLOVA_LOG_LEVEL_DEBUG
```

Available levels are `FLOVA_LOG_LEVEL_ERROR`, `WARN`, `INFO`, `DEBUG`, and
`TRACE`. Set `FLOVA_LOGGING_ENABLED=0` to remove Flova logging calls entirely.
Do not log credentials, provisioning tokens, device secrets, or complete
sensitive payloads.

## Universal firmware from the SDK

Build and upload the universal composition from your own project. The selected
toolchain creates the complete board image, including the bootloader and
partition table; no prebuilt firmware download is required.

For ESP32, install `FlovaSDK` from Arduino Library Manager or the PlatformIO
Registry and include the universal composition:

```cpp
#include <Arduino.h>
#include <FlovaSDK.h>

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
