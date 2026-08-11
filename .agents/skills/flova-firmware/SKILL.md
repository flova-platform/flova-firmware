---
name: flova-firmware
description: Engineering rules for Flova embedded firmware and SDK. Use for implementation, refactoring, debugging, protocol, networking, memory, performance, and testing work on constrained embedded targets including ESP8266.
---

# Flova Firmware Engineering

Build production-grade embedded code with deterministic memory use, bounded resource consumption, low overhead, explicit ownership, and predictable failure behavior.

## Mandatory rules

- Do not introduce unbounded memory usage.
- Flova-owned runtime hot paths must not dynamically allocate.
- Do not place large buffers on the stack.
- Do not use recursion.
- Use fixed-capacity queues and buffers.
- Validate every network-controlled length before processing.
- Bound frame sizes, collection sizes, strings, nesting depth, and queues.
- Stream or chunk large payloads.
- Reuse TLS and networking resources.
- Use explicit state machines for lifecycle management.
- Keep ownership and lifetimes obvious.
- Keep durable backlog out of RAM.
- Avoid unnecessary copies and intermediate representations.
- Keep portable protocol logic independent from ESP8266 APIs.
- Measure memory instead of assuming it is safe.
- Reject unexplained RAM and flash regressions.
- Stress reconnect and failure paths.
- Host-test and fuzz protocol parsing.

## CBOR and protocol

Use bounded typed decoding.

Do not build a dynamic generic CBOR DOM.

Encode directly from typed application state into bounded transport buffers.

Use compact numeric protocol fields and opcodes in hot paths.

Read `references/protocol.md` before changing protocol framing, CBOR schemas, WSS behavior, message limits, or large-payload handling.

## Datastreams

Datastream keys are human and API identifiers.

Runtime protocol identity is a stable server-assigned `uint16_t` `DatastreamId`.

ESP8266 runtime traffic uses IDs, never string names.

Do not use hashes, runtime string maps, CBOR string-reference tables, or connection-scoped aliases for datastream identity.

ESP8266 supports a maximum of 64 active datastream runtime entries.

Read `references/datastreams.md` before modifying datastream identity, binding, configuration, SDK declarations, or wire representation.

## Memory

Treat runtime memory behavior as part of correctness.

Track:

- current free heap
- minimum free heap
- largest free block
- heap fragmentation
- stack headroom

Large reusable buffers must use explicitly owned bounded storage.

Read `references/memory.md` before modifying memory ownership, buffers, queues, TLS resources, persistent state, or ESP8266 runtime layout.

## Testing

Changes affecting networking, protocol, ownership, persistence, or memory must include appropriate failure-path tests.

Protocol parsers must remain host-testable and fuzzable.

Reconnect behavior must not progressively degrade heap state.

Read `references/testing.md` before changing parsers, reconnect logic, queues, persistence, TLS lifecycle, configuration handling, or OTA behavior.

## Architecture

Prefer small concrete components with narrow responsibilities.

Do not introduce abstraction layers without a concrete portability, testability, or ownership benefit.

Avoid desktop and enterprise architecture patterns that hide allocation, ownership, or runtime cost.

Optimize architecture before micro-optimizing instructions.

## Definition of done

A firmware or SDK change is complete only when:

- resource usage remains bounded
- failure behavior is deterministic
- ownership is clear
- protocol input is validated
- memory budgets remain acceptable
- relevant tests pass
- no progressive heap degradation is introduced
- target-specific code remains isolated from portable core logic
