# Provisioning

Provisioning has two responsibilities:

1. Product code obtains an Engine HTTPS base URL, one-time token, and network
   credentials through SoftAP, BLE, serial, Ethernet tooling, cellular
   bootstrap, factory injection, or a PLC engineering application.
2. The board opens verified Flova Link v1 WSS, generates its 32-byte device
   secret locally, receives deterministic-CBOR configuration records, and
   commits them transactionally.

There is no device-facing `POST /api/device/provision` compatibility path.
ESP wrappers expose only local `/status` and `/provision` setup endpoints. The
local request is bounded; ESP8266 returns
`202 {"ok":true,"status":"accepted"}`, persists the handoff, and reboots.
The clean bootstrap boot never starts the setup HTTP server or SoftAP; it joins
Wi-Fi, synchronizes UTC for certificate validation without blocking the main
loop, and uses the bounded
`link_url` supplied by the app's compile-time deployment configuration.

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
`CONFIG_END` validates the final count and checksum, durably marks
the inactive generation complete, atomically promotes it, and then returns the
final acknowledgement. Configuration is neither JSON nor a reconstructed
whole-document in RAM. After Engine reports bootstrap committed, the board
saves credentials and reboots; the active configuration is restored into
runtime objects before normal authenticated Link startup.

Runtime restore validates every persisted record and the complete transfer
digest before touching mappings or GPIO. It tries the newest generation first,
discards a corrupt bank, and falls back to the previous verified bank. Normal
state and command traffic remains disabled until runtime publishes
`config_reported` with the exact restored generation and checksum. A mismatch
causes Engine to retransmit the desired generation; a completed runtime update
is applied only after a clean reboot, so no connection can observe a mixture of
old and new records.

The A/B storage implementation must keep the last active generation readable
until promotion succeeds. An interruption, malformed record, checksum failure,
or lost acknowledgement leaves it active; retrying an already-durable record or
final acknowledgement is idempotent. Only after the final commit can Engine
redeem the bootstrap token, rotate credentials, and report bootstrap committed.

An interrupted transfer leaves the token pending and never replaces the last
valid configuration. Transient bootstrap failures retry up to three persisted
attempts with a clean reboot between attempts; terminal failures return to the
setup AP and expose only sanitized
`last_error_code` plus `can_retry` through `/status`. A reset detected during
bootstrap is reported as `firmware_reset_during_bootstrap` and does not loop
forever. The product app reconnects to its normal network and treats Engine's
provisioning-status endpoint as authoritative.

Once committed credentials exist, the board always boots into runtime and the
setup AP stays disabled. Wi-Fi, NTP, TLS, and Link outages are runtime offline
conditions handled by native Wi-Fi/SNTP recovery and bounded Link reconnect;
they never make the device unprovisioned. Engine may report a transient attempt
as `verifying_runtime`, while `provisioned` and `presence` remain independent
states. Provisioning becomes complete only after the matching runtime
`config_reported` message is durably processed.

The app's compile-time Link URL is authoritative and must use `wss://`.
Firmware has no Flova Cloud hostname fallback. Tokens, secrets, and
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

Development-era credential layouts are intentionally not migrated.
