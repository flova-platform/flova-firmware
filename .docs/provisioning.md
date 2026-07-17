# Provisioning

Provisioning has two separate responsibilities:

1. Product code obtains an Engine URL and one-time token through SoftAP, BLE, serial, Ethernet tooling, cellular bootstrap, factory injection, or a PLC engineering application.
2. `flova::Provisioner` performs the common Engine redemption, validates the complete session, stores it, and seeds UTC.

Implement `flova::EngineClient` using the target's HTTPS client. `beginRedeem()` starts `POST /api/device/provision`; `poll()` remains non-blocking and returns a validated `ProvisioningSession`. Production builds reject non-HTTPS Engine URLs unless `FLOVA_ALLOW_INSECURE_PROVISIONING` is explicitly defined.

Generic requests send `provisioning_token`, `hardware_id`, `board_type`,
`firmware_target`, the structured `protocol` identity, `schema_version`, and
board capabilities. Engine returns the negotiated limits, server UTC
milliseconds, device/template identity, MQTT credentials, topics, and
datastream metadata. ESP transports may additionally send `chip_id` and
`mac_address` as hardware identifiers; those fields do not define a separate
protocol.

Call `Provisioner::run()` from the main loop. It writes a pending record first, commits the validated session, then removes the pending record. Failure never deliberately deletes the previous session. Never log tokens, MQTT passwords, or complete responses.

Initial provisioning resolves Engine's desired published template. The applied
template remains unchanged until the device reports the matching version and
checksum. ESP setup status exposes only a sanitized `last_error`; successful
provisioning responses are never logged because they contain MQTT credentials.

The application-level contract is Flova device provisioning, not ESP
provisioning. The current ESP wrappers implement its local HTTP transport using
`/status` and `/provision`; another board may expose the same endpoints without
ESP libraries. During a request the wrappers use AP+STA mode and keep the local
endpoint reachable. `200` means the configuration was stored, while structured
`4xx`/`5xx` responses are definitive retryable failures. There is no early
accepted response.
