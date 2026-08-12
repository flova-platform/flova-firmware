# Custom Arduino phone provisioning

This example uses the same public typed facade as the static custom client
through an explicit `FlovaEsp32` or `FlovaEsp8266` board class, but does not
contain Wi-Fi credentials or device credentials.

On first boot it starts a SoftAP named `Flova-Setup-...` and exposes:

- `GET /status`
- `POST /provision`

The phone sends the Wi-Fi SSID/password, Engine Link URL, and activation token.
The device generates its secret locally, persists the handoff, reboots, and
bootstraps over Flova Link before entering the normal datastream runtime.

Build for both supported Arduino targets:

```sh
pio run -e custom-arduino-provisioning-esp32
pio run -e custom-arduino-provisioning-esp8266
```

The application may call `client.startProvisioning()` from its own factory
reset button or service command to re-enter setup mode.
