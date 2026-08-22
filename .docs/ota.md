# OTA updates

Firmware binaries are built outside Engine and uploaded as immutable `.bin` releases. Engine sends the install offer over the authenticated Device Link; the device downloads the artifact directly over HTTPS.

## Build and release configuration

Build the non-transactional universal artifacts with `pio run -e universal-esp32 -e universal-esp8266` and compile the release version with `-DFLOVA_FIRMWARE_VERSION=\"1.2.3\"`. The universal profiles also embed a `flovainf` metadata tag containing the version, firmware target, boot layout, and OTA contract. The Console extracts those values from the `.bin`; operators only need to choose the file and enter the version. OTA does not add a device setting or provisioning secret.

The universal ESP32 and ESP8266 profiles use board-owned OTA implementations
and report the actual free sketch capacity at runtime. ESP32 uses its inactive
OTA slot; ESP8266 uses the platform's staged-copy updater. Flova
configuration records still use a separate A/B storage transaction; that
storage generation is not a firmware-image slot.

The offer includes `version`, `firmware_target`, `sha256`, `size_bytes`, and `artifact_url`. Devices verify the artifact server's public certificate and compute SHA-256 while streaming into the platform updater. A mismatch aborts before the new boot image is activated; there is no OTA-specific key to provision.

## Device Link lifecycle

- Desired offer: Device Link `ota_desired` frame.
- Device reports: Device Link `ota_reported` frame.
- Report states: `notified`, `installing`, `rebooting`, `candidate`, `health_checking`, `confirmed`, `rollback_requested`, `rolled_back`, or `failed`.
- Final success requires a matching release/install heartbeat and a stable boot state. A candidate must publish an authenticated heartbeat and stay connected for 30 seconds; it is rolled back after a two-minute health deadline.

OTA is staged by the board-owned Device Link service and executed from the
board loop. Firmware binary data never travels through Device Link. Passive
ESP SDK facades advertise OTA as unavailable until the application explicitly
calls `enableOta()`; universal firmware enables it by policy.

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
platform-specific rollback mechanics; the internal Arduino runtime owns offer validation,
Link lifecycle reports, disconnect-before-download, and restart behavior.

Custom boards may call `setOtaProfile(strategy, bootLayoutVersion,
rollbackCapable)` after constructing the ESP32/ESP8266 facade. The target string
is supplied independently with `setFirmwareTarget()`. This lets an application
advertise its real partition/layout contract without pretending to be a
universal firmware. A board must set `rollbackCapable=true` only when its
boot-control and health-confirmation path has physical acceptance coverage.

Custom binaries that do not enable the build metadata tag can still be uploaded
through the Console advanced path by supplying their target and boot layout.

The default board profile reports the real board strategy (`ab` for the
inactive/staged updater) but `rollbackCapable=false` until the application
supplies a physically accepted health-confirmation path. A single maximum-size application partition cannot support native OTA
because flash cannot rewrite the running image. Stock ESP8266 eboot stages and
copies the image without automatic rollback. Console rollout eligibility is
fail-closed when a device has no positive reported image capacity, and
non-transactional devices require explicit risk acknowledgement.
