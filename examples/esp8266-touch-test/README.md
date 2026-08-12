# ESP8266 touch/LED offline test

This is a flashable integration test for the current ESP8266 SDK and mobile
provisioning flow.

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

If your template uses different keys, change `LED_DATASTREAM_KEY` and
`TOUCH_DATASTREAM_KEY` in `src/main.cpp`. The keys must match Engine exactly.

Build and flash:

```sh
pio run -e esp8266-touch-test -t upload
pio device monitor -b 115200
```

On a fresh device:

1. The firmware starts a `Flova-Setup-...` SoftAP.
2. Use the native mobile provisioning flow to send Wi-Fi credentials and the
   device's short-lived Link provisioning handoff.
3. Wait for the device to finish bootstrap and appear online.
4. Open the device dashboard and verify the LED and Touch Sensor datastreams.

Touching the sensor produces a rising-edge event on `TOUCH_SENSOR` and toggles `LED`
through `led.write(...)`. The LED callback is also used for mobile commands,
schedules, and automations, so the test exercises one consistent datastream
path.

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

This example intentionally ignores template GPIO mappings so the physical test
always uses D1/D2. It is suitable for testing the SDK/protocol/mobile flow,
not for validating universal firmware hardware-mapping behavior.
