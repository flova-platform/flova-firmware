# Testing and release checks

The repository currently uses PlatformIO compile contracts because both public wrappers depend on Arduino framework types and board SDKs. Run:

```sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
pio run -e universal-esp32 -e universal-esp8266
pio run -e datastream-api-esp32 -e datastream-api-esp8266
pio test -e test-esp32 -e test-esp8266 --without-uploading --without-testing
git diff --check
```

The native C++11 suite executes cache-only reads, refresh/report behavior, rejection preserving state, cloud/local handler convergence, acknowledgements, stale revision suppression, and KeepLatest reconnect coalescing. It also compiles the complete custom-board example without Arduino. The board examples compile the typed APIs on ESP32 and ESP8266; `--without-uploading` verifies Unity test binaries, while connected boards execute them.

Before a release, test one physical ESP32 and ESP8266 for provisioning, MQTT reconnect, an accepted remote relay write, a rejected unsafe write, offline local state followed by reconnect, duplicate command delivery, factory reset, and persistent cache restoration. Record flash and RAM output from PlatformIO for both universal targets.

Provisioning validation must include an Engine response larger than 4096 bytes,
a reboot from the stored snapshot, and recovery after corrupting the current
snapshot while leaving its backup intact. Confirm that serial diagnostics report
response size and heap state without printing MQTT credentials or response
payloads.

For schedules, also verify capability heartbeat values, retained manifest
installation, `schedules/reported`, execution while disconnected, reboot
restoration without replaying missed occurrences, renewal publication, and
safe expiry on both physical targets.
On ESP8266, install a near-limit retained manifest and reboot without erasing
flash. Confirm MQTT connects without a watchdog reset and record the free heap,
largest free block, and fragmentation diagnostics around MQTT allocation.

Native host execution is recommended follow-up work. It requires extracting Arduino `String` behind a native-compatible value boundary; do that as a focused change rather than adding a production fake.
