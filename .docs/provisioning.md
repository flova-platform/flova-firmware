# Provisioning

Provisioning has two responsibilities:

1. Product code obtains an Engine WSS Link URL, one-time token, and network
   credentials through SoftAP, BLE, serial, Ethernet tooling, cellular
   bootstrap, factory injection, or a PLC engineering application.
2. The board opens verified Flova Link v1 WSS, generates its 32-byte device
   secret locally, receives deterministic-CBOR configuration records, and
   commits them transactionally.

The SDK separates the setup channel from Device Link. Every channel produces
the same bounded `flova::ProvisioningHandoff { linkUrl, token }` and calls
`flova.provision(handoff)`. Wi-Fi credentials belong only to the universal
Wi-Fi composition; they are not embedded in or replayed through the portable
handoff. BLE, serial, Ethernet, cellular, radio gateways, factory tooling, and
application-owned HTTP servers therefore use the same runtime entry point.

The normal ESP SDK is passive: it observes whether the application's network
is connected, supplies private persistent storage and UTC bootstrap for TLS,
and never changes Wi-Fi mode, owns a server, touches GPIO, or reboots. Its
`attachProvisioning(server)` helper only registers `/status` and `/provision`
on a borrowed server. The universal ESP composition is the explicit owner of
SoftAP, its private setup server, Wi-Fi credential storage, GPIO mappings, OTA,
and restart policy.

There is deliberately no second `CommunicationAdapter` abstraction.
`flova::Link` is already the communication contract. A custom runtime
transport implements or injects `flova::Link`; provisioning only supplies the
bounded material that transport needs to start.

There is no device-facing `POST /api/device/provision` compatibility path.
ESP wrappers expose only local `/status` and `/provision` setup endpoints. The
local request is bounded; universal ESP8266 returns
`202 {"ok":true,"status":"accepted"}`, persists the handoff, then stops the
setup server and changes from SoftAP to station mode. Bootstrap joins Wi-Fi,
synchronizes UTC for certificate validation without blocking the main loop,
and uses the bounded `link_url` supplied by the provisioning handoff.

Bootstrap uses the same binary WSS framing as normal operation; there is no
preface or second bootstrap wire format. Its `bootstrap_auth` frame carries
the one-time token, device-generated raw secret, hardware identity, firmware
target, and board capabilities. Engine validates but does not redeem the token
at this point.

After authentication, Engine uses the normal Link transaction:

```text
CONFIG_BEGIN -> CONFIG_RECORD* -> CONFIG_END
```

Every transfer message is one binary WSS frame, no larger than 512 bytes
including the 12-byte header. `CONFIG_BEGIN` declares the generation, checksum,
record count, limits, and generation-scoped compact IDs. Each `CONFIG_RECORD`
is a bounded CDDL CBOR record. Firmware validates it, writes it to the inactive
generation in watchdog-safe chunks, reads it back, and acknowledges that
sequence before accepting the next record. Bootstrap does not allocate runtime
datastream or hardware objects while BearSSL is active.
`CONFIG_END` validates the final count and checksum and durably marks the
inactive generation complete. Firmware then schema-decodes and semantically
validates every stored record without changing GPIO or live SDK state. Only a
fully valid generation is atomically promoted and acknowledged. Configuration
is neither JSON nor a reconstructed whole-document in RAM. After Engine reports
bootstrap committed, the board saves and verifies credentials, restores the
promoted configuration, and enters normal authenticated Link runtime in the
same boot.

Runtime restore validates every persisted record and the complete transfer
digest before touching mappings or GPIO. It tries the newest generation first,
discards a corrupt bank, and falls back to the previous verified bank. Normal
state and command traffic remains disabled until runtime publishes
`config_reported` with the exact restored generation and checksum. A mismatch
causes Engine to retransmit the desired generation; a completed runtime update
is applied only after a clean reboot, so no connection can observe a mixture of
old and new records. Universal ESP firmware automatically restarts after a
normal runtime configuration update. Advanced compositions remain in
`RestartRequired` and expose the reason until the application performs its
platform-specific restart.

The A/B storage implementation must keep the last active generation readable
until promotion succeeds. An interruption, malformed record, checksum failure,
or lost acknowledgement leaves it active; retrying an already-durable record or
final acknowledgement is idempotent. Only after the final commit can Engine
redeem the bootstrap token, rotate credentials, and report bootstrap committed.

An interrupted transfer leaves the token pending and never replaces the last
valid configuration. Transient bootstrap failures retry up to three persisted
attempts. A reset during bootstrap resumes the verified pending handoff and
consumes the next attempt; it does not erase an otherwise valid claim. Terminal
failure returns universal firmware to its setup AP, while passive SDK firmware
returns to `AwaitingProvisioning`. Both expose only a sanitized
`last_error_code` and `can_retry`. The product app treats Engine's
provisioning-status endpoint as authoritative.

Once committed credentials exist, the board always boots into runtime and the
setup AP stays disabled. Wi-Fi, UTC bootstrap, TLS, and Link outages are
runtime offline conditions handled by the owning application or universal
adapter plus bounded Link reconnect;
they never make the device unprovisioned. Engine may report a transient attempt
as `verifying_runtime`, while `provisioned` and `presence` remain independent
states. Provisioning becomes complete only after the matching runtime
`config_reported` message is durably processed.

The Link URL supplied by `ProvisioningHandoff` is authoritative and must use
`wss://`. Firmware has no Flova Cloud hostname fallback. Tokens, secrets, and
configuration contents must never be logged.

The supported ESP8266 target profile is verified BearSSL WSS with a
2,048-byte RX / 512-byte TX Link allocation. Shared IRAM must be enabled and
the adapter must fail closed when the required TLS memory profile is not
available. Link and OTA never coexist: Link disconnects before OTA opens its
separate verified HTTPS client with a 16 KiB RX / 512-byte TX profile.
Certificate-chain and hostname verification are mandatory; plaintext,
`setInsecure()`, and fingerprint-only fallbacks are prohibited.

For a custom board, carry this same Link v1 WSS frame and streamed
configuration contract. Its adapter must report provisioning complete only after
the configuration storage transaction has succeeded, and must preserve the
previous generation on failure.

Configuration image version 3 deliberately invalidates older development-era
credential layouts; there is no pre-MVP migration. From this version onward,
ordinary firmware uploads that preserve the filesystem/NVS retain the same
validated Flova identity. Universal Wi-Fi data is stored independently from
the protocol handoff.
