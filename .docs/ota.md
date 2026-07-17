# OTA updates

Firmware binaries are built outside Engine and uploaded as immutable `.bin` releases. Engine sends the install offer over the device's authenticated MQTT connection; the device downloads the artifact directly over HTTP(S).

## Build and release configuration

Build the non-transactional universal artifacts with `pio run -e universal-esp32 -e universal-esp8266` and compile the release version with `-DFLOVA_FIRMWARE_VERSION=\"1.2.3\"`. OTA does not add a device setting or provisioning secret.

Transactional recovery is opt-in. ESP32 developers can use `universal-esp32-ab-4m` or `universal-esp32-ab-8m`, or copy that environment's partition file and build flags into their board profile. The default environment never replaces the developer's partition table and reports `otaStrategy=none`.

The offer includes `version`, `firmware_target`, `sha256`, `size_bytes`, and `artifact_url`. Devices compute SHA-256 while streaming into the platform updater. A mismatch aborts before the new boot image is activated. OTA requires TLS plus per-device broker credentials and topic ACLs in production; there is no OTA-specific key to provision.

## MQTT v1 lifecycle

- Desired offer: `flova/v1/devices/{device_id}/ota/desired`
- Device reports: `flova/v1/devices/{device_id}/ota/reported`
- Report states: `notified`, `installing`, `rebooting`, `candidate`, `health_checking`, `confirmed`, `rollback_requested`, `rolled_back`, or `failed`.
- Final success requires a matching release/install heartbeat and a stable boot state. A candidate must publish an authenticated MQTT heartbeat and stay connected for 30 seconds; it is rolled back after a two-minute health deadline.

OTA is staged in the MQTT callback and executed from `FlovaDevice::loop()`. Binary data never travels through MQTT.

## Custom boards

Ordinary OTA only needs `FlovaOtaInstaller::install()` from `FlovaOta.h`. Transactional recovery additionally implements `FlovaBootControl` from `FlovaBootControl.h` and attaches it with `setBootControl()`. The adapter owns transport, flash slots, candidate confirmation, and rollback mechanics. The shared SDK owns offer parsing, target checks, lifecycle reports, reboot identity, and the health deadline.

Every transactional layout has a stable `bootLayoutVersion`; Engine only installs a release on a device with the same layout and enough `maxImageBytes`. Flova supplies `esp32-4m-ab-v1` (1.875 MiB slots) and `esp32-8m-ab-v1` (3.875 MiB slots). The adapter disables transactional claims at runtime if its inactive slot is smaller than the selected preset.

The default ESP32 profile uses the board's platform-managed OTA layout but immediately confirms a pending image, so Flova's health-gated rollback is not active. A single maximum-size ESP32 application partition cannot support native OTA because flash cannot rewrite the running image. ESP8266 also reports `otaStrategy=none`; stock eboot stages and copies the image without automatic rollback. Non-transactional devices require explicit risk acknowledgement in Console.
