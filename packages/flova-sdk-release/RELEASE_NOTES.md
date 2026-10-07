FlovaSDK 0.3.10 adds transport-neutral datastream helpers for custom firmware.

- `FLOVA_DATASTREAM`, `FLOVA_REPORT`, and `FLOVA_ON_WRITE` work with Flova Link
  and injected Arduino `PubSubClient` MQTT transports.
- MQTT command values are decoded according to the declared datastream type and
  acknowledgements are emitted automatically.
- Existing direct Link datastream APIs remain compatible; local cache helpers
  remain Link-only.

FlovaSDK 0.3.9 adds direct Ethernet device support for custom ESP32 firmware.

- Added the `FlovaEsp32Ethernet` facade for LAN8720 devices.
- Added compile-time template, device name, factory token, and Link URL
  metadata for direct-provisioned custom firmware.
- Added a short Ethernet example using the `FLOVA_*` datastream helpers.
- Added bounded Ethernet and Linux socket seams for application-owned network
  implementations.

FlovaSDK 0.3.8 adds safe Link preparation for board-owned power management.

- Added `prepareForPowerDown()` with bounded drain, runtime safety checks, and
  explicit `Busy`, `Draining`, `Ready`, and `Failed` states.
- Exposed the helper through the Arduino, ESP32, ESP32 BLE, ESP8266, and
  universal board facades.
- Deep sleep, Wi-Fi shutdown, and wake-source configuration remain owned by
  each board application.

FlovaSDK 0.3.7 adds beginner-friendly custom datastream helpers while preserving the existing typed API.

- Added `FLOVA_DATASTREAM`, `FLOVA_WRITE`, `FLOVA_READ`, `FLOVA_REPORT`, `FLOVA_ON_WRITE`, and `FLOVA_HAS_VALUE`.
- Custom ESP32 and ESP8266 board headers load the helpers automatically.
- Converted custom Arduino, provisioning, datastream, touch, BLE, OTA, and portable board examples to the helper syntax.
- Added host coverage for boolean and text helper usage.

FlovaSDK 0.3.6 supports ESP32 and ESP8266 projects in Arduino IDE and PlatformIO.

- Added bounded status snapshots and edge-triggered status listeners for
  lifecycle, local network/TLS, Flova Link, runtime readiness, configuration
  generation, and sanitized error transitions.
- Exposed the same status helpers through all ESP32 and ESP8266 SDK facades.
- Added host coverage and a custom Arduino example for status observation.

- Universal ESP32 and ESP8266 firmware now accepts symbolic pin references.
  Examples include ESP8266 `A0`/`ADC0`/`TOUT` and ESP32 `ADC1_CH4`.
- Pin capability and electrical validation stays inside the universal firmware;
  custom SDK applications continue to own their hardware configuration.
- Configurations with symbolic references are sent only after a device advertises
  support, preserving compatibility with existing firmware.

- Canonical Device Link encoding is consistent across build tools.
- ESP32 uses one task for the complete Link socket lifetime.
- WebSocket control and application traffic share an ordered writer.
- ESP8266 uses stock BearSSL; the framework patch is removed.
- ESP8266 PlatformIO and Arduino examples select the shared IRAM heap profile
  required for full-record TLS buffers.
- Temporary bootstrap failures retain pending provisioning and retry with backoff.
- OTA waits for Link output and closure before starting its download.
- A FLOVA startup banner identifies the SDK version and target.
- The local setup portal is Persian, RTL, and follows the Flova lime-green design.

ESP8266 TLS connection operations can pause the application loop. Link and OTA
require sufficient contiguous heap and the documented shared IRAM profile. OTA
also requires an appropriate flash layout. Hardware acceptance is recorded
separately from package compilation; consult the validation report for the
release.
