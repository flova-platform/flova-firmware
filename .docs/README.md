# Flova firmware documentation

Use this index instead of scanning every document.

- [Codebase map](codebase-map.md): package ownership, dependency direction, and the portable-core boundary.
- [Architecture](architecture.md): ownership boundaries and the local-first lifecycle.
- [Datastream API](datastream-api.md): public types, operations, modes, policies, and examples.
- [Device Link protocol](cloud-protocol.md): one-frame WSS binary transport, deterministic CDDL CBOR, acknowledgements, retries, and reconnect behavior.
- [Custom boards](custom-boards.md): STM32, PLC, RTOS, gateway, and portable adapter integration.
- [Provisioning](provisioning.md): Link v1 bootstrap and streamed transactional A/B configuration.
- [OTA updates](ota.md): authenticated Device Link offers, SHA-256 verification, and custom-board adapters.
- [Clock and offline data](clock-and-offline.md): Engine UTC synchronization and reconnect policies.
- [Time and schedules](time-and-schedules.md): UTC ownership and developer-local wall-clock schedules.
- [Testing and release checks](testing.md): compile matrix, layer/CDDL/hot-path guards, hardware checks, and memory review.
- [Protocol assets](../protocol/README.md): CDDL ownership, generated codecs, vectors, and regeneration workflow.
- [Repository scripts](../scripts/README.md): focused validation, generation, and PlatformIO tools.

The root [README](../README.md) is the short public entry point. These documents are the maintained technical contract for contributors.
