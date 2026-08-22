# ESP8266 touch/LED offline test

This is a small flashable ESP8266 custom touch/LED example. The sketch owns its
fixed GPIOs, Wi-Fi connection, and HTTP provisioning routes.

Wiring:

```text
touch module digital OUT -> D1 / GPIO5
external LED + resistor   -> D2 / GPIO4
```

The example uses these exact template keys:

```text
LED          = boolean read/write
TOUCH_SENSOR = boolean read-only
```

If your template uses different keys, change the two string literals in
`src/main.cpp`. The keys must match Engine exactly.

Build and flash:

```sh
pio run -e esp8266-touch-test -t upload
pio device monitor -b 115200
```

Set `WIFI_SSID` and `WIFI_PASSWORD` in `src/main.cpp`. On a fresh device:

1. Connect the device to the configured Wi-Fi network.
2. Use the app's custom provisioning request against the device HTTP server to
   send the short-lived Link handoff.
3. Wait for the device to finish bootstrap and appear online.
4. Open the device dashboard and verify the LED and Touch Sensor datastreams.

Touching the sensor produces a rising-edge event on `TOUCH_SENSOR` and toggles
`LED` through `led.write(...)`. Mobile commands, schedules, automations, and
local writes use the same datastream path.

For the offline test, temporarily turn off or disconnect the Wi-Fi access
point after the device is provisioned. Touch the sensor several times. The LED
must continue toggling locally because `LED` uses `OfflinePolicy::KeepLatest`.
Restore Wi-Fi and wait for Link to reconnect; the latest LED state and touch
state should synchronize to Engine and become visible in the mobile app.

Disable or remove any automation that also toggles `LED` from `TOUCH_SENSOR` before this
test. Otherwise one physical touch causes both local firmware logic and the
Engine automation to write the LED, which can produce two toggles or a race.
After testing the local behavior, re-enable the automation separately to test
the cloud-controlled path.

This custom example intentionally does not use template pin mappings. The
sketch owns D1/D2 directly; advanced applications can change those constants
or replace the hardware behavior entirely.

## OTA-enabled variant

The OTA-enabled copy uses the same wiring and datastream behavior:

```sh
pio run -e esp8266-touch-test-ota
```

The first installation must be wired/factory flashed. After provisioning, the
device reports the `esp8266-touch-test-ota` target and accepts matching HTTPS
firmware offers after validating their declared size and SHA-256 checksum.
ESP8266 OTA uses the platform's staged-copy updater and has no automatic
rollback; keep wired recovery available for a failed post-update boot.

For a release build, provide an explicit version with
`-DFLOVA_FIRMWARE_VERSION=\"x.y.z\"` and upload the resulting ESP8266 binary
as a separate artifact from the ESP32 BLE image.
