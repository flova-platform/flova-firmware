# Custom ESP32 Ethernet device

This sketch connects an ESP32 with a LAN8720 RMII PHY to Flova over Ethernet.
`FlovaEsp32Ethernet` owns DHCP readiness, certificate-validated WSS, the
existing bounded Device Link transport, ESP32 NVS, and direct factory-token
bootstrap. The application only supplies PHY parameters and datastream
   handlers. The sketch uses the same `FLOVA_*` helpers as other custom Flova
   applications; no Wi-Fi network or BLE setup is started.

## Configure

1. Create a direct Ethernet device in Console using a published template.
   Define a Boolean datastream named `enabled` in that template.
2. Set the four macros at the top of `src/main.cpp` or supply them through
   private build flags:
   - `FLOVA_DEVICE_TEMPLATE_ID`: the selected template ID.
   - `FLOVA_DEVICE_NAME`: an application label (the Console name remains authoritative).
   - `FLOVA_DEVICE_PROVISION_TOKEN`: the factory token shown at device creation.
   - `FLOVA_DEVICE_LINK_URL`: your deployment's complete `wss://` Device Link URL.
3. If your board differs from the default LAN8720 wiring, pass the PHY
   parameters to `FlovaEsp32Ethernet client(...)`. The defaults expect a PHY
   supplying a 50 MHz clock to GPIO0. Check boot-strapping requirements and
   RMII pin reservations against the board schematic.
4. Build and upload from the repository root:

   ```sh
   pio run -e custom-ethernet-esp32
   pio run -e custom-ethernet-esp32 -t upload
   pio device monitor
   ```

5. Connect Ethernet to a router with DHCP and internet access, then scan the
   device's activation QR in Studio/PWA. Engine permits bootstrap only after
   claim. The sketch retries every 30 seconds while awaiting provisioning.
   After credentials and configuration are committed, Studio opens the device
   dashboard and the `enabled` write handler runs from `client.run()`.

The token identifies the Engine-owned device/template association. The
template macro is required by this example as deployment metadata; it is not
sent as a replacement for that association. The device-name macro is available
for your application UI and does not rename the Engine device.

Keep each factory token private and unique per device. Do not commit actual
credentials or print them. Bootstrap generates the runtime secret locally;
later boots restore it from NVS rather than submitting the factory token.

## Network and TLS

`FlovaEsp32Platform` already uses ESP32's system TCP/IP stack, so its TLS
socket works through Ethernet despite the Arduino `WiFiClientSecure` class
name. `FlovaEsp32Ethernet` observes Ethernet link/IP readiness instead of
Wi-Fi status. UDP UTC bootstrap runs over the same interface for certificate
checks.
The default Flova trust roots apply; private deployments can provide
`FLOVA_TLS_ROOT_CERTS` before the platform header. Never disable certificate
verification.

This example does not enable OTA or configure GPIO mappings. Add your own
hardware writes inside `setEnabled()` and avoid PHY-reserved pins. A W5500 or
another external TCP/IP stack instead needs its own validated TLS socket and
the generic `FlovaEthernetPlatform` adapter.

Compilation does not establish DHCP, PHY timing, cable reconnect, WSS
bootstrap, or hardware command acceptance. Those require the selected board
and PHY. Linux laptop simulation still needs a concrete native WSS Link
implementation; this sketch is an ESP32 hardware example.
