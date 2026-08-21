# Universal ESP32 BLE provisioning

This example is the BLE setup-channel variant of universal ESP32 firmware. It
uses Espressif Unified Provisioning over BLE with Security 1 and a null proof
of possession for the MVP. The BLE session is encrypted, but the phone is not
authenticated; do not release this setup mode as production-secure.

The device advertises as `Flova-BLE-XXXXXX`. The Flova Android development
client scans for that prefix, sends the Flova Link handoff through the custom
`flova-handoff` endpoint, and then sends Wi-Fi credentials through the standard
Espressif provisioning endpoint. After the handoff is durably accepted, BLE is
stopped and released before WSS bootstrap begins.

Build from the repository root:

```sh
pio run -e universal-esp32-ble
```

The existing `universal-esp32` target remains SoftAP-based. Provisioning
transport is a firmware capability, not a template setting in Flova Frontend.

The 4 MiB DevKit BLE image uses the 2 MiB single-app partition and has OTA
disabled for this MVP. Use a larger-flash production board and an explicit
partition layout before enabling OTA for BLE firmware.
