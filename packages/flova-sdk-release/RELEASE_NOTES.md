FlovaSDK 0.3.3 supports ESP32 and ESP8266 projects in Arduino IDE and PlatformIO.

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
