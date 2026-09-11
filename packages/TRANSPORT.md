# Device Link transport

Flova owns one Link socket per device. Protocol parsing, configuration, and
hardware callbacks run from `device.run()`. ESP32 performs socket operations in
one worker; ESP8266 uses public stock BearSSL from the application loop.

## Ordering and lifetime

All writes use one transport slot. Submission copies bytes; completion means the
client accepted them, not that Engine acknowledged them. WebSocket control
frames wait for the current frame. ESP32's receive ring stops reading when full,
letting TCP apply backpressure. Cancellation invalidates the current generation;
the worker closes it before allowing another connection. Destruction waits for
worker completion.

OTA enters maintenance, drains output with a five-second deadline, and waits for
Link closure before allocating its HTTPS session. Failed drain aborts the OTA.
During installation, local application processing may pause.

Runtime configuration activation retains the final CONFIG_ACK while the transmit
slot is busy (up to five seconds), then drains it and the WebSocket close before
requesting a restart. No further configuration or OTA work runs during this
handoff. A failed send or drain still permits activation of the verified durable
generation; after restart, CONFIG_REPORTED confirms the active checksum to Engine.
The transfer ACK alone does not prove that the new configuration is running.

## ESP8266

Stock BearSSL can block inside connection and write operations, including
cryptographic work that cannot be interrupted by a Flova polling deadline.
Applications requiring uninterrupted low-latency hardware handling should use
ESP32 or hardware peripherals that operate independently of the application loop.

Link and OTA use 16,384-byte TLS RX and 512-byte TX profiles. The 512-byte Flova
frame bound does not constrain TLS records. Memory preflight checks free heap
and largest block; actual TLS allocation can still fail. Shared IRAM is optional.
The stock DRAM profile must be physically validated with the intended sketch's
memory use before deployment. A compile result alone proves neither available
TLS heap nor successful provisioning.

## Build and diagnostics

The package compiles unchanged upstream zcbor through canonical C wrappers;
consumers need no encoding flags or core patch. `FLOVA_VERSION` is the single
Flova release version; `FLOVA_SDK_VERSION` and `FLOVA_FIRMWARE_VERSION` are
compatibility names for the same value in their respective reporting paths.
`FLOVA_NO_DEFAULT_BANNER` disables the startup banner. Call `Serial.begin()`
before the board facade's `begin()` to see it.

Temporary bootstrap failures retain the verified handoff and retry with jittered
backoff, capped at 60 seconds. Server-declared token rejection requires a new
handoff. Codec/configuration defects stop and preserve their error for diagnosis.
Retry counters remain in RAM. Factory reset remains the explicit way to erase
device configuration.

SoftAP provisioning starts station association in AP+STA mode and keeps the
setup portal responsive until the station has an address. The runtime retires
the setup channel before starting clock and Link work; failed credentials can
therefore be corrected without erasing the accepted handoff.
