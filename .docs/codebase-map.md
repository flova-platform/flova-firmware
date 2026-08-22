# Firmware codebase map

The repository is a collection of reusable libraries plus selected firmware
applications. The root `src/` directory is only a documented PlatformIO
source anchor; it contains no firmware implementation. Environments select an
application under `examples/*/src` with `build_src_filter`.

```text
packages/flova-embedded-sdk portable C++11 domain runtime
packages/flova-arduino      generic Arduino services and Device Link core
packages/flova-esp32        ESP32 transport/TLS/OTA and universal composition
packages/flova-esp8266      ESP8266 transport/TLS/OTA and universal composition
examples/                   firmware applications and compile contracts
protocol/                   CDDL authority, generated codecs, and vectors
third_party/                vendored protocol dependencies
test/host/                  CMake host tests for the portable runtime/protocol
test/test_datastream/       PlatformIO Unity tests for the portable runtime
scripts/                    generators and bounded-contract checks
.docs/                      maintained architecture and contributor contracts
```

The package ownership table is also available in
[`packages/README.md`](../packages/README.md).

## Dependency direction

```text
portable SDK core
        ^
        | domain services and bounded records
Arduino core adapters
        ^
        | bounded WebSocket framing, services, and platform seam
ESP32 / ESP8266 board packages
        ^
        | TLS, socket, OTA, pin policy, and explicit full-device ownership
selected example application
```

`FlovaDevice.h` and the `flova::` namespace are the canonical portable SDK
surface. Arduino applications select passive `FlovaEsp32` or `FlovaEsp8266`
facades explicitly. No-code applications select `FlovaUniversalEsp32` or
`FlovaUniversalEsp8266` to transfer whole-device ownership. The portable core
remains unchanged in either composition.

Portable core code must not include Arduino, ESP, GPIO, Wi-Fi, WebSocket, TLS,
or filesystem headers. The generic Arduino package may provide bounded Device
Link framing and service orchestration, but it must not select a board transport
or call board APIs. ESP32 and ESP8266 packages own their concrete TLS/socket/OTA
and pin policies; other Arduino boards can supply the same
`FlovaArduinoPlatform` seam or implement `flova::Link` directly. Configuration
installation, OTA, scheduling, and hardware mapping remain separate optional
services.

Generated zcbor and third-party files are protocol/build inputs, not hand-edited
SDK source. Keep protocol schema changes in `protocol/flova-link-v1.cddl` and
regenerate the checked-in artifacts and vectors together.
