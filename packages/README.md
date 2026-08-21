# Flova packages

These directories are reusable libraries, not application entry points.

| Package | Owns | Must not own |
| --- | --- | --- |
| `flova-embedded-sdk` | Portable `flova::Device`, datastream semantics, bounded configuration and scheduling | Arduino, ESP, GPIO, Wi-Fi, WebSocket, TLS, or filesystem APIs |
| `flova-arduino` | Arduino clocks, storage, logging, TLS, OTA, Device Link, and separate setup/network/clock/identity contracts | Product-specific GPIO mappings or a second domain runtime |
| `flova-esp32` | ESP32 setup provisioning, runtime network, boot/storage policy, identity, and board composition | Portable SDK semantics or provisioning-owned runtime connectivity |
| `flova-esp8266` | ESP8266 setup provisioning, runtime network, TLS/resource policy, boot/storage, identity, and board composition | Portable SDK semantics or provisioning-owned runtime connectivity |

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
The public protocol package is named `flova-link`; generated zcbor headers
remain an internal implementation detail of that package.

`FlovaDevice.h` is the portable runtime. New board ports implement the four
`flova::` service interfaces and compose an explicit board class as documented
in `.docs/custom-boards.md`.
