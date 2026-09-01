# Flova Link v1 device protocol

Public Flova APIs use JSON over HTTPS. Device Link is separate: verified binary
`wss://`, deterministic CBOR payloads, and no JSON, MQTT topics, Base64
envelopes, Phoenix Channels, or human-readable JSON property names on the
device wire.

`protocol/flova-link-v1.cddl` is the single canonical machine-readable
wire-contract. Firmware and Engine derive their representations from it and
share deterministic-CBOR conformance vectors. Handwritten payload layouts and
generic value codecs are not an alternative protocol definition.

## Frame and transport

Each WebSocket **binary** message contains exactly one complete frame. Text
messages, WebSocket compression, fragmented/reassembled application frames,
and multiple frames in one message are rejected.

```text
+---------------+---------------+----------------+----------------+
| message_type  | flags         | payload_length | message_id     |
| uint8         | uint8         | uint16         | uint64         |
+---------------+---------------+----------------+----------------+
| deterministic CBOR payload                                      |
+-----------------------------------------------------------------+
```

The outer header is always 12 bytes. A complete frame is at most 512 bytes,
leaving a strict 500-byte CBOR payload maximum. Oversized WebSocket messages
and a header/payload-length mismatch are rejected before CBOR decoding. The
outer header alone supports routing, authentication and connection-generation
checks, rate limits, flow control, ACK correlation, and basic frame validation.
`message_id` stays stable for a durable retry, including after reconnect.

## Restricted deterministic CBOR

The CDDL profile accepts only declared unsigned/signed integers, booleans,
float32, explicitly required float64, bounded UTF-8 strings, bounded byte
strings, and definite-length bounded arrays/maps. Maximum nesting is four.
Indefinite values, tags, bignums, duplicate keys, arbitrary value trees,
unknown structures, invalid narrowing, malformed required UTF-8, trailing
bytes, and all values beyond their CDDL bounds are rejected without persistent
state changes. Unknown optional fields are accepted only when the active CDDL
type explicitly permits them, then skipped without allocation.

Serialization is deterministic: shortest valid integer/length forms, definite
lengths, canonical map-key order, and the exact CDDL-selected float width.
Equivalent firmware and Engine values therefore encode to identical bytes.
Hot-path telemetry/state, heartbeats, ACKs, command results, and time/flow
messages use fixed-schema CBOR arrays. Extensible configuration/control records
use ascending integer-keyed maps.

## Compact identities and configuration

`CONFIG_BEGIN` assigns compact datastream integer IDs scoped to a configuration
generation. Subsequent telemetry, state, commands, and command results carry
that ID and generation rather than repeatedly transmitting a datastream UUID.
Frames for an unknown or stale generation are rejected.

Bootstrap and normal updates use the same transactional sequence:

```text
CONFIG_BEGIN -> CONFIG_RECORD* -> CONFIG_END
```

`CONFIG_BEGIN` declares generation, schema version, checksum, record count,
limits, and ID mappings. A `CONFIG_RECORD` holds one bounded CDDL unit such as
a datastream with nested hardware mapping, a system record, schedule metadata,
an ordered schedule-occurrence chunk, or a safety policy. Firmware decodes one
fixed record, validates it, immediately writes it
to inactive A/B storage, verifies that write, and sends its bounded ACK before
accepting another record. `CONFIG_END` verifies count/checksum, finalizes the
inactive bank, validates the complete generation without hardware side effects,
and only then atomically promotes it. Nested configuration is preserved semantically, but a
complete configuration is never rebuilt in RAM. More records use more transfer
time and flash, not more working memory.

SoftAP may hand off Wi-Fi/bootstrap inputs. The board persists the handoff,
returns the local acknowledgement, stops the setup server, and changes to the
configured network before Wi-Fi/TLS bootstrap; an intermediate reboot is not
required.
Engine bootstrap uses this same verified WSS CBOR record exchange. Credentials become
authoritative only after the staged generation is verified and committed; final
provisioning confirmation is idempotent across a lost ACK, including replay of
an already committed generation. Engine presence telemetry is best effort and
cannot delay or suppress `bootstrap_committed`. There is no separate
HTTPS/JSON provisioning configuration format.

## Reliability, TLS, and memory

CBOR changes representation, not delivery semantics. Newest-connection-wins,
connection-generation fencing, authentication deadlines, full-jitter reconnect,
flow control, permanent rejection, command correlation, and command-ID
deduplication remain invariants. A command with a non-zero UTC expiry is
rejected before its handler when time is invalid or the deadline has passed.
Best-effort telemetry can be shed; durable
messages retain their ID across retry/reconnect and receive an application ACK
only after the required Engine persistence. Commands run only in the device
loop and report an applied value or rejection without duplicate state reports.
Engine also acknowledges accepted heartbeats with the existing ingestion ACK.
After observing that capability once, firmware treats a missing heartbeat ACK
as a stale application connection and reconnects without waiting for TCP to
notice a half-open path. Older Engine deployments remain compatible because
the watchdog is armed only after the first heartbeat ACK.

Certificate-chain and hostname validation are mandatory: no plaintext,
`setInsecure()`, fingerprint-only validation, or downgrade. Official ESP8266
Link WSS uses the Cloudflare-compatible BearSSL **2,048-byte RX / 512-byte TX**
profile and shared IRAM. OTA destroys/releases Link first, then streams
over separate verified HTTPS using **16,384-byte RX / 512-byte TX**; OTA
artifacts never accumulate in RAM.

ESP8266 runtime Link writes are cooperative: one complete masked WebSocket
frame remains in the adapter's fixed TX workspace while BearSSL and lwIP accept
bounded chunks. The device loop never waits for a peer TCP acknowledgement.

The ESP8266 Link codec and configuration transaction use fixed frame, zcbor,
decoded-record, and persistence workspaces. Runtime objects are restored before
normal Link startup and are not allocated while bootstrap records are being
persisted. Peer-provided lengths never choose allocation sizes.

This document defines the locked v1 target contract. A deployment may claim
conformance only after its vectors, fuzz, memory, and hardware tests pass.
