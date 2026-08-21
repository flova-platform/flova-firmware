# Architecture

## Product boundary

Flova Cloud runs user automations, schedules, notifications, and cross-device
workflows. Firmware contains developer-authored product logic and safety rules;
devices never execute user-authored scripts or automation bytecode.

Datastreams are the shared contract across firmware, Cloud, mobile, and
operations dashboards. A physical change, local condition, user action, or
cloud automation resolves to a datastream value, while firmware remains
authoritative about safe hardware writes.

Public APIs remain JSON-over-HTTPS. The constrained device plane is Flova Link:
verified binary WSS, a fixed 12-byte header, and deterministic bounded CBOR
defined only by version-controlled CDDL. Generated zcbor structures isolate
wire details, so SDK users keep their strongly typed read/write, state, command
result, configuration, and OTA APIs without handling CBOR.

## Local-first lifecycle

```text
local write or Device Link command
  -> validate compact ID, generation, type, and safety limits
  -> invoke registered hardware handler from Device::run()/loop()
  -> on acceptance only, update cache and optional persistence
  -> report state or retain one bounded durable snapshot offline
  -> return locally or report one correlated command result
```

`value()` never touches hardware or the network. `report()` observes state and
never invokes an actuator. Transport callbacks
only decode/queue bounded records and never invoke hardware directly.

`FlovaClient::begin()` is the single owner of Flova-private storage startup.
It mounts or opens storage before any provisioning, configuration, schedule,
or history access and fails closed when startup fails. Board facades delegate
to that lifecycle instead of mounting storage themselves, so both direct SDK
composition and universal firmware have the same ordering. Physical storage
adapters reject access before successful startup and expose bounded record and
capacity limits; application-owned filesystems and servers remain otherwise
untouched.

## Portable core and protocol boundary

`FlovaDevice.h` remains Arduino/ESP/GPIO/exception/RTTI-free. Board adapters own
WSS, TLS, and storage. Applications own Wi-Fi, servers, GPIO, OTA permission,
and reboot policy unless they deliberately select a universal full-device
composition. The C++11 core uses fixed
arrays and explicit callbacks, not dynamic protocol containers or generic
CBOR/JSON object trees.

The frame layer validates the 12-byte header and the 512-byte complete-frame
limit before zcbor. Encoders/decoders use generated schema-aware fixed C/C++
structures and caller-provided buffers. CDDL fixes every scalar/collection
bound and nesting is capped at four. There is no handwritten TLV/value codec,
format negotiation, compatibility decoder, or JSON intermediary in this path.
Each WSS binary message is exactly one complete frame; payload capacity is 500
bytes. Larger logical work is a CDDL-defined bounded record/chunk sequence.
OTA images stay outside Link as streamed verified HTTPS downloads.

## Streamed configuration and provisioning

```text
CONFIG_BEGIN -> validate/stage metadata and compact IDs
CONFIG_RECORD -> fixed decode -> validate -> inactive A/B write -> verify -> ACK
CONFIG_END -> finalize -> side-effect-free semantic validation -> atomic promotion -> ACK
```

`CONFIG_BEGIN` establishes a configuration generation and compact datastream ID
mapping. Subsequent telemetry/state/commands use that ID plus generation rather
than UUIDs. Records preserve bounded nested hardware mappings, schedules,
system settings, and safety policies but are persisted independently, reusing
one working record. RAM therefore stays essentially constant as configuration
count rises. Schedule metadata and at-most-16-value occurrence chunks use the
same record transaction; the fixed compiler rejects duplicate, missing, or
out-of-order chunks before installation. Commands may carry a bounded UTC
expiry deadline; expiry-protected commands are rejected when the device clock
is invalid or the deadline has passed.

Every setup channel ends in one bounded Link URL/token handoff. Universal
firmware obtains it through SoftAP and stores Wi-Fi separately; a normal SDK
application can supply the same handoff through its existing server, BLE,
serial, cellular, or factory flow. Once connected, Engine
bootstrap uses the same WSS transactional CBOR exchange and A/B installer as a
normal update. Credentials are authoritative only after verified commit, and
final confirmation is idempotent if the connection is lost.

## ESP8266 memory and TLS profile

ESP8266 permits one active TLS workload. Official Link WSS uses verified
BearSSL **2,048-byte RX / 512-byte TX**, shared IRAM, certificate-chain and
hostname validation. Generic/self-hosted endpoints use the full profile until
explicitly certified for the smaller receive record. No plaintext,
`setInsecure()`, or fingerprint-only fallback is permitted.

Link owns one fixed 512-byte receive buffer, one fixed 512-byte transmit
buffer, bounded zcbor state, and one fixed decoded-record workspace. After
transport initialization, encode/decode, telemetry/state, commands, ACKs,
configuration installation/storage, and reconnect perform zero dynamic heap
allocation and never allocate from a peer-controlled length.

OTA releases Link first, then opens its independent streamed verified HTTPS
client with ESP8266 **16,384-byte RX / 512-byte TX** buffers; it never retains
the artifact in RAM.

## Reliability and validation

Message IDs, durable application ACKs, idempotent retries, permanent
rejections, connection-generation fencing, newest-connection-wins,
authentication deadlines, flow control, overload handling, command correlation,
and full-jitter reconnect backoff remain protocol invariants. Best-effort
telemetry may be shed; durable messages keep their ID until persistence is
acknowledged.

Required acceptance includes generated-schema/cross-language canonical-CBOR
vectors, 512/513-byte frame tests, malformed-input and fuzz coverage,
transactional A/B power-loss/reconnect coverage, and ESP8266 TLS/WSS/max-frame/
nested-record/reconnect memory instrumentation. The static
`scripts/check_flova_link_contract.sh` guard rejects dynamic JSON, generic
trees, heap allocation, dynamic containers, and Arduino `String` in explicit
Link/configuration/storage paths; exceptions are exact, documented, and only
valid outside those paths.

This is the required architecture, not evidence that a particular build has
completed every acceptance test. Release claims require measured results.

ESP applications use explicit board classes composed over `flova::Device`.
The passive board header composes application-owned network observation with
Flova-private identity, entropy, storage, TLS, UTC bootstrap, and bounded
runtime services. Universal classes replace observation with a board-owned
network runtime and add temporary provisioning, hardware, OTA, and restart
ownership. Provisioning, runtime networking, TLS clock readiness, and identity
remain separate service contracts. Shared runtime
code does not select a board with preprocessor branches. New board integrations use the service seams described in
`.docs/custom-boards.md`.
