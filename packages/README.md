# Flova packages

These directories are reusable libraries, not application entry points.

| Package | Owns | Must not own |
| --- | --- | --- |
| `flova-device-sdk` | Portable `flova::Device`, datastream semantics, bounded configuration and scheduling | Arduino, ESP, GPIO, Wi-Fi, WebSocket, TLS, or filesystem APIs |
| `flova-arduino` | Arduino clocks, storage, logging, TLS, OTA, and Device Link integration | Product-specific GPIO mappings or a second domain runtime |
| `flova-esp32` | ESP32 provisioning, boot/storage policy, and board composition | Portable SDK semantics |
| `flova-esp8266` | ESP8266 provisioning, TLS/resource policy, boot/storage, and board composition | Portable SDK semantics |

`FlovaDevice.h` and `FlovaTransport.h` remain temporarily available for the
existing ESP applications. They are migration-only compatibility headers; new
board ports must include `FlovaCore.h` and implement the four `flova::` service
interfaces documented in `.docs/custom-boards.md`.
