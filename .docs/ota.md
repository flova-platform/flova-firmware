# OTA updates

Firmware binaries are built outside Engine and uploaded as immutable `.bin` releases. Engine sends the install offer over the authenticated Device Link; the device downloads the artifact directly over HTTPS.

## Build and release configuration

Build the non-transactional universal artifacts with `pio run -e universal-esp32 -e universal-esp8266` and compile the release version with `-DFLOVA_FIRMWARE_VERSION=\"1.2.3\"`. OTA does not add a device setting or provisioning secret.

The universal ESP32 and ESP8266 profiles use the board platform's standard OTA
layout and report non-transactional OTA capability. Flova configuration records
still use a separate A/B storage transaction; that storage generation is not a
firmware-image slot.

The offer includes `version`, `firmware_target`, `sha256`, `size_bytes`, and `artifact_url`. Devices verify the artifact server's public certificate and compute SHA-256 while streaming into the platform updater. A mismatch aborts before the new boot image is activated; there is no OTA-specific key to provision.

## Device Link lifecycle

- Desired offer: Device Link `ota_desired` frame.
- Device reports: Device Link `ota_reported` frame.
- Report states: `notified`, `installing`, `rebooting`, `candidate`, `health_checking`, `confirmed`, `rollback_requested`, `rolled_back`, or `failed`.
- Final success requires a matching release/install heartbeat and a stable boot state. A candidate must publish an authenticated heartbeat and stay connected for 30 seconds; it is rolled back after a two-minute health deadline.

OTA is staged by the board-owned Device Link service and executed from the
board loop. Firmware binary data never travels through Device Link.

Before downloading, the shared runtime publishes `installing`, disconnects
Device Link, and releases its TLS session. ESP8266 reuses the device's single
pre-parsed CA list with the shared-IRAM HTTPS TLS profile and existing 512-byte
firmware streaming buffer. A failed download reconnects Device Link before
reporting `failed`; a missing or insufficient TLS memory profile reports
`resource_unavailable`. WSS and HTTPS are never active together.

## Custom boards

An Arduino OTA service accepts the bounded `FlovaLinkOtaOffer` and returns a
`flova::OtaInstallResult`. A custom board may implement the same operation with
its platform updater. The board adapter owns HTTPS, flash writing, and any
platform-specific rollback mechanics; `FlovaClient` owns offer validation,
Link lifecycle reports, disconnect-before-download, and restart behavior.

Custom boards that provide health-gated firmware rollback may add a bounded
board-owned boot-control service and advertise that capability only after its
recovery path has physical acceptance coverage. The standard Flova ESP32 and
ESP8266 builds do not provide that transactional firmware-image contract.

The default ESP32 profile uses the board's platform-managed OTA layout but immediately confirms a pending image, so Flova's health-gated rollback is not active. A single maximum-size ESP32 application partition cannot support native OTA because flash cannot rewrite the running image. ESP8266 also reports `otaStrategy=none`; stock eboot stages and copies the image without automatic rollback. Non-transactional devices require explicit risk acknowledgement in Console.
