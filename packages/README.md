# Flova packages

These directories are reusable libraries, not application entry points.

| Package | Owns | Must not own |
| --- | --- | --- |
| `flova-device-sdk` | Portable `flova::Device`, datastream semantics, bounded configuration and scheduling | Arduino, ESP, GPIO, Wi-Fi, WebSocket, TLS, or filesystem APIs |
| `flova-arduino` | Arduino clocks, storage, logging, TLS, OTA, and Device Link integration | Product-specific GPIO mappings or a second domain runtime |
| `flova-esp32` | ESP32 provisioning, boot/storage policy, and board composition | Portable SDK semantics |
| `flova-esp8266` | ESP8266 provisioning, TLS/resource policy, boot/storage, and board composition | Portable SDK semantics |

Arduino users who want to keep their own application code should install the
matching board package and include `<FlovaEsp32.h>` or `<FlovaEsp8266.h>`.
Both concrete classes expose the same typed facade; the application retains
ownership of GPIO, sensors, and long-running work. `FlovaProvisioningConfig`
enables the selected board package's persisted Wi-Fi and phone setup lifecycle.
The public protocol package is named `flova-link`; generated zcbor headers
remain an internal implementation detail of that package.

`FlovaCore.h` is the portable runtime. New board ports implement the four
`flova::` service interfaces and compose an explicit board class as documented
in `.docs/custom-boards.md`.
