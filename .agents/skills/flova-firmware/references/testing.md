# Firmware Testing Standard

## Goal

Flova firmware is not production-ready because normal operation succeeds.

It is production-ready when bounded memory, parser safety, lifecycle correctness, and failure recovery remain stable under repeated stress.

## Required failure scenarios

Test at minimum:

- repeated connect/disconnect cycles
- Wi-Fi loss during WSS operation
- router reboot
- DNS failure
- TLS handshake failure
- certificate rejection
- server unavailable
- authentication failure
- server-side socket closure
- malformed WebSocket frames
- truncated CBOR
- malformed CBOR
- maximum-sized valid frame
- oversized frame
- maximum supported nesting
- excessive nesting
- rapid telemetry
- queue saturation
- interrupted configuration update
- OTA interruption

## Reconnect stability

Run at least:

```text
10,000+ connect/reconnect cycles
```

Observe:

```text
free heap
minimum free heap
largest free block
fragmentation
stack headroom
```

After repeated reconnects, memory health must remain approximately stable.

Progressive degradation is a bug.

## Long-running tests

Exercise:

- sustained normal operation
- unstable Wi-Fi
- repeated server restarts
- telemetry bursts
- configuration changes
- idle periods followed by bursts

Track memory low-water marks over the entire run.

## Host-side tests

Portable core logic must compile and run outside the MCU.

Host-test at minimum:

- CBOR decoding
- CBOR encoding
- message validation
- state transitions
- retry/backoff logic
- queue-full policy
- datastream binding
- datastream dispatch
- configuration validation

Keep ESP8266-specific APIs out of the portable core.

## Fuzzing

The real firmware protocol decoder must be fuzzable on a host machine.

Feed:

- random bytes
- truncated CBOR
- corrupted CBOR
- invalid major types
- invalid lengths
- excessive nesting
- duplicate keys
- unexpected fields
- integer boundaries
- maximum payloads
- zero-length values
- incomplete maps
- incomplete arrays

Use host tooling such as:

```text
AddressSanitizer
UndefinedBehaviorSanitizer
libFuzzer
```

Do not maintain a separate simplified parser only for tests.

## Acceptance

A change affecting protocol, networking, queues, persistence, ownership, TLS lifecycle, configuration, or OTA is accepted only when:

- relevant failure tests pass
- bounded-memory assumptions still hold
- malformed input is rejected safely
- reconnects do not progressively degrade memory
- host tests remain green
- parser fuzzing reveals no crash, overflow, use-after-free, or undefined behavior

## Memory redesign regression checks

Run the TLS-budget host boundary tests, 64-key multi-frame binding tests, and
all-text capacity/int64 safety tests alongside the usual suite. Build the
the universal ESP8266 default-MMU profile. Run the image budget checker both
normally and with `--stock-dram`; distinguish a reviewed image-size ceiling from
physical product acceptance. Maintain failure evidence in `packages/MEMORY.md`
rather than weakening limits to make the gate green.

Do not flash or manipulate connected devices when the requested validation is
code/build only. Record provisioning, reconnect, OTA and fragmentation as
unverified when hardware acceptance was not performed.

## Phase ownership regression rules

Run `flova-compact-runtime-test` for storage-backed descriptors, 64 maximum
keys/text values, bounded delivery fairness, stale ACKs and failed policy writes.
Retest pause/checkpoint/restore failures, callback borrows during disconnect,
and the exported SDK whenever phase ownership changes. Report all four
`--stock-dram` phases; never treat linked-size savings as runtime acceptance.
