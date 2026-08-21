# ESP32 touch/LED offline test

This is the ESP32 custom touch/LED example. The sketch owns its fixed GPIOs,
Wi-Fi connection, and HTTP provisioning routes.

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

If your template uses different keys, change the two string literals in the
selected example source. The keys must match Engine exactly.

Build and flash:

```sh
pio run -e esp32-touch-test -t upload
# BLE provisioning variant:
pio run -e esp32-touch-test-ble -t upload
pio device monitor -b 115200
```

Set `WIFI_SSID` and `WIFI_PASSWORD` in `src/main.cpp`. On a fresh device, use
the app's custom provisioning request against the device HTTP server to send
the short-lived Link handoff. The universal SoftAP and BLE no-code variants
are separate examples.

Touching the sensor reports `TOUCH_SENSOR` and toggles `LED` through
`led.write(...)`. With Wi-Fi disconnected after provisioning, local toggles
continue because `LED` uses `OfflinePolicy::KeepLatest`; after reconnect, the
latest LED and touch states synchronize to Engine. The LED value is also
persisted and reapplied to GPIO2 after a power cycle.

Disable any automation that also toggles `LED` from `TOUCH_SENSOR` while testing
the local behavior, or one physical touch may be toggled twice.

This custom example intentionally does not use template pin mappings. The
sketch owns GPIO2/GPIO4 directly; advanced applications can change those
constants or replace the hardware behavior entirely.

The BLE target uses the 2 MiB single-app partition and has OTA disabled for the
current ESP32 DevKit BLE image. It also uses the bounded BLE profile, so its
runtime datastream capacity is smaller than the SoftAP target. BLE provisioning
currently uses Security 1 with a null proof of possession for MVP testing.
