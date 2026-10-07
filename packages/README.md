# Flova packages

These directories are reusable libraries, not application entry points.

| Package | Owns | Must not own |
| --- | --- | --- |
| `flova-embedded-sdk` | Portable `flova::Device`, datastream semantics, bounded configuration and scheduling | Arduino, ESP, GPIO, Wi-Fi, WebSocket, TLS, or filesystem APIs |
| `flova-arduino` | Generic Arduino clocks, storage, logging, bounded Device Link framing, and the board-platform seam | ESP/TLS/updater APIs, product-specific GPIO mappings, or a second domain runtime |
| `flova-linux` | POSIX clock, file storage, logging, and supervisor-owned OTA staging for Linux devices | Arduino, ESP board policy, or a Linux service manager's process lifecycle |
| `flova-esp32` | ESP32 socket/TLS/OTA, setup provisioning, runtime network, boot/storage policy, identity, pin policy, and board composition | Portable SDK semantics or provisioning-owned runtime connectivity |
| `flova-esp8266` | ESP8266 BearSSL/socket/OTA, resource policy, setup provisioning, runtime network, boot/storage, identity, pin policy, and board composition | Portable SDK semantics or provisioning-owned runtime connectivity |

Arduino users who want to keep their own application code should install the
matching board package and include `<FlovaEsp32.h>` or `<FlovaEsp8266.h>`.
Both concrete classes expose the same typed application API; the application
retains ownership of Wi-Fi, servers, GPIO, sensors, reboot policy, and
long-running work. `begin()` restores Flova-private state or waits for a
bounded `ProvisioningHandoff`; it does not start a setup AP.
`FlovaUniversalEsp32` and `FlovaUniversalEsp8266` are the full-device, no-code
compositions. Advanced Arduino ports may include `<FlovaArduino.h>` and compose
`FlovaClient` with their own Link, temporary provisioning, runtime network, TLS
clock bootstrap, identity, and storage services.
Custom Ethernet applications may include `<FlovaEthernetAdapter.h>` and inject
an Ethernet/TLS socket implementation. The adapter owns only the bounded Link
platform seam; the application owns the Ethernet chip library, pins, DHCP, and
certificate setup.
ESP32 LAN8720 applications can use `<FlovaEsp32Ethernet.h>` for the complete
direct-device composition and automatic factory-token bootstrap.
For an ESP32 LAN8720 composition reusing the native TCP/IP and TLS stack, see
[`examples/custom-ethernet-esp32`](../examples/custom-ethernet-esp32).
`FlovaEsp32` and `FlovaEsp8266` use Flova Link by default. Applications that
prefer MQTT may pass their `PubSubClient` directly to the board facade and call
`begin(deviceId, deviceSecret)`. Wi-Fi, TLS, and the MQTT client remain
application-owned; the SDK provides bounded JSON topic handling. See
[`examples/custom-mqtt`](../examples/custom-mqtt).
The public MQTT endpoint is `mqtt.flova.ir:8883`.
The public protocol package is named `flova-link`; generated zcbor headers
remain an internal implementation detail of that package.

`FlovaDevice.h` is the portable runtime. New board ports implement the four
`flova::` service interfaces and compose an explicit board class as shown in
`examples/custom-board-basic`.

Linux applications install `Flova::Linux` with CMake, inject the POSIX services
from `FlovaLinux.h`, and provide their own `flova::Link`. Raspberry Pi service
startup, restart, and executable activation remain application/supervisor
responsibilities.
