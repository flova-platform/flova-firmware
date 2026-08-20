# ESP32 touch/LED offline test

This is the ESP32 counterpart of the explicit ESP8266 touch/LED integration
test. It owns the GPIO behavior and uses the ESP32 SoftAP provisioning flow.

Wiring:

```text
external LED + resistor   -> D2 / GPIO2
touch module digital OUT  -> D4 / GPIO4
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
pio run -e esp32-touch-test -t upload
pio device monitor -b 115200
```

On a fresh device, use the native mobile provisioning flow to connect to the
`Flova-Setup-...` SoftAP, send Wi-Fi credentials and the short-lived Link
provisioning handoff, then verify `LED` and `TOUCH_SENSOR` in the dashboard.

Touching the sensor reports `TOUCH_SENSOR` and toggles `LED` through
`led.write(...)`. The same LED callback handles mobile commands, schedules, and
automations. With Wi-Fi disconnected after provisioning, local toggles continue
because `LED` uses `OfflinePolicy::KeepLatest`; after reconnect, the latest LED
and touch states synchronize to Engine.

Disable any automation that also toggles `LED` from `TOUCH_SENSOR` while testing
the local behavior, or one physical touch may be toggled twice.

This example intentionally uses fixed GPIO2/GPIO4 wiring and ignores template
GPIO mappings. It tests the SDK/protocol/mobile flow, not universal firmware
hardware mapping.
