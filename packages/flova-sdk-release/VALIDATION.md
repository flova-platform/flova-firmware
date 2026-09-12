# FlovaSDK 0.3.4 release validation

This document records the FlovaSDK 0.3.4 package and release validation. CI
publication and physical-device acceptance remain separate gates from the
local build checks below.

Passed during implementation:

- Eight CMake/CTest host suites, including symbolic pin resolution for ESP32
  and ESP8266, 10,000 WebSocket reconnect cycles,
  canonical codec vectors, partial-write/control ordering, copied TX ownership,
  graceful drain, and bounded retry checks.
- The seven host suites with Clang AddressSanitizer and UndefinedBehaviorSanitizer.
- `scripts/check_flova_link_contract.sh`,
  `scripts/check_flova_public_surface.sh`, and `git diff --check`.
- `pio run -e universal-esp32 -e universal-esp8266` and
  `pio run -e datastream-api-esp32 -e datastream-api-esp8266`.
- `pio test -e test-esp32 -e test-esp8266 --without-uploading --without-testing`
  and `pio test -e test-bootstrap-esp8266 --without-uploading --without-testing`.
- `scripts/check_esp8266_stack_usage.py`.
- Detached exported-archive PlatformIO compilation on both targets.

Remaining release gates:

- Stock-core Arduino compilation: `arduino-cli core update-index` failed with
  HTTP 403 from downloads.arduino.cc. A subsequent
  `arduino-cli core install esp8266:esp8266@3.1.2` reached GitHub downloads but
  failed with `context deadline exceeded`. The local PlatformIO ESP8266 cache
  was previously patched and therefore does not prove stock-core compatibility.
  Both Arduino targets are included in CI for a clean runner.
- Re-export and test the final archive after the last source changes. Temporary
  build/package directories disappeared when the execution environment refreshed;
  do not depend on the earlier /tmp paths.
- ESP32 cancellation/destruction and reconnect stress on actual FreeRTOS, plus
  PWA provisioning through bootstrap commit and power-cycle recovery.
- Physical ESP8266 default-DRAM heap acceptance, TLS, reconnect fragmentation,
  and OTA success/failure. Full-record TLS adds memory requirements which remain
  unverified on hardware.
- CI execution, publication, and installation of the published version.

The old BearSSL patch script was removed from source and export; Git retains its
history. The unrelated universal-esp32-ble README change was preserved.
