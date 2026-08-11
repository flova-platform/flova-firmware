# Repository scripts

The scripts directory contains small, deterministic tools for protocol
generation, contract checks, and PlatformIO integration.

## Protocol generators

The protocol source of truth and generated-artifact workflow is documented in
[`protocol/README.md`](../protocol/README.md). Generated files are checked in
so host builds and PlatformIO builds do not require a generator at compile
time.

```sh
scripts/generate_flova_link_cbor.sh
python3 scripts/generate_flova_link_vectors.py
```

Run the generators only after changing the CDDL or vector definitions. Review
the generated diff and run the native protocol tests afterward. Never edit
`protocol/generated/` by hand.

Python bytecode, caches, build output, and PlatformIO output are ignored by
the repository and must not be committed.

## ESP8266 framework integration

ESP8266 Link environments run
`packages/flova-arduino/scripts/patch_esp8266_bearssl_nonblocking.py` before
compilation. The `flova-arduino` `library.json` manifest runs the same script
automatically for ESP8266 consumers and it is a no-op for ESP32. The script
accepts only the pinned Arduino framework version and exact upstream source
hashes, then adds the cooperative BearSSL write API used by
`ArduinoDeviceLink`. It is idempotent and fails closed if the installed
framework changes.
