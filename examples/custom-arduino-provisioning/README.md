# Custom Arduino phone provisioning

This example adds Flova provisioning routes to an application-owned Arduino
web server. The SDK does not start, stop, or replace that server and does not
change the application's Wi-Fi mode.

The application starts its normal Wi-Fi connection and server, then calls
`flova.attachProvisioning(server)` to register:

- `GET /status`
- `POST /provision`

The setup channel sends the Engine Link URL and activation token. The device
generates its secret locally, persists the handoff, and bootstraps over the
already-managed network without a reboot. BLE, serial, cellular, factory, or
other channel code can instead call
`flova.provision(ProvisioningHandoff(...))`. The current mobile SoftAP flow is
provided by universal firmware.

This is also the model for alternate channels. A BLE, serial, cellular,
Ethernet, or factory adapter can decode its own bounded input and call:

```cpp
flova::ProvisioningHandoff handoff(linkUrl, shortLivedToken);
client.provision(handoff);
```

The handoff contains only the authoritative WSS Link URL and short-lived
token. The device generates its own secret. Wi-Fi credentials and network
ownership stay with the application or universal composition.

Build for both supported Arduino targets:

```sh
pio run -e custom-arduino-provisioning-esp32
pio run -e custom-arduino-provisioning-esp8266
```

The application may call `client.startProvisioning()` from its own factory
reset button. This clears Flova-private identity and waits for another handoff;
it does not alter network or server state.
