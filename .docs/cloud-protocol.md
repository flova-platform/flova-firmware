# Cloud protocol v1

The wire protocol is identified by a structured envelope instead of a product
name embedded in a version string:

```json
{
  "protocol": { "name": "flova", "version": 1 },
  "schema_version": 1
}
```

MQTT topics live under `flova/v1/devices/<device-id>/`. Devices publish
`heartbeat`, `state`, `command-results`, `config/reported`,
`schedules/reported`, `schedules/renew`, `ota/reported`, and `time/request`;
they subscribe to `commands`, `config/desired`, `schedules/desired`,
`ota/desired`, and `time/response`. Broker ACLs restrict every device to its
own topic prefix and direction. OTA offers contain metadata and SHA-256 only;
binary artifacts are downloaded directly over HTTP(S).

The heartbeat advertises physical board capacities. Engine combines those
capacities with deployment policy and sends the negotiated operational limits
in configuration. A capacity is an implementation fact (for example, storage
available in a particular board build), not a product plan limit. Firmware
never silently substitutes a product-level maximum.

`firmware_system.status_led` is an optional deployment setting with a `GPIO<n>`
pin and `active_low` polarity. When present, universal ESP firmware keeps the
indicator solid while MQTT is connected and blinks it during reconnects; when
absent, firmware does not claim a status pin or use a board-specific default.

`firmware_system.factory_reset` is also optional and is consumed only by the
universal ESP32/ESP8266 wrappers. It either references a mapped digital-input
datastream or reserves a dedicated `GPIO<n>` input with an `active_low`
polarity. Reset is accepted only during the first 60 seconds after boot after
three taps, a continuous 10-second hold, and a confirming release. A missing
setting reserves no reset pin.

Engine persists the last capability report and exposes the negotiated schedule
slots plus exact compiled-manifest byte usage to clients. Until a board reports
capabilities, schedules remain cloud-only and the UI must not claim offline
availability. Disabled schedules do not consume an active manifest slot.

Commands are processed only inside the device loop, deduplicated by command
ID, validated against the datastream contract, and acknowledged after the
hardware handler accepts or rejects them. A cloud-originated write emits one
`command-results` report containing the applied value, correlation ID, and
acknowledged desired version; it does not also emit a duplicate `state`
message. Engine retries the retained desired state until that version is
acknowledged. Device-originated observations continue to use `state`, whose
ingestion acknowledgement clears the bounded KeepLatest snapshot. Offline
state uses the negotiated, bounded keep-latest or history policy.

`state` accepts either one `key`/`value` reading or an atomic `readings` array.
A batch has one stable envelope `message_id`, at most 32 logical readings, and
must fit the negotiated message-size limit. Engine acknowledges the envelope
only after every reading is validated and committed; firmware retries the
exact envelope until then.

Schedule manifests are renewable caches of Engine-owned schedules. Engine
compiles IANA timezone rules into a rolling UTC horizon. The default server
policy is 90 days and is refreshed daily, after edits, on reconnect, and when
the device reports a low horizon. Firmware uses the manifest's `renew_before`
value; it does not own the 90-day or renewal policy. An expired manifest stops
executing safely, and missed occurrences are never replayed.
