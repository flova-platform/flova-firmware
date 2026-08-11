# Flova Link protocol assets

This directory contains the versioned Flova Link v1 wire contract and the
firmware artifacts derived from it. Keep the ownership order below clear:

1. [`flova-link-v1.cddl`](flova-link-v1.cddl) is the authoritative wire schema.
   It defines message types, field shapes, bounds, and the deterministic CBOR
   profile.
2. [`message-types.csv`](message-types.csv) is a human-readable message
   catalog. It is useful for review and tooling, but it does not override the
   CDDL.
3. [`conformance-vectors.json`](conformance-vectors.json) contains deterministic
   frame examples. The matching C header under `generated/` is derived from it.
4. [`generated/firmware/`](generated/firmware/) contains zcbor codecs, types,
   vector declarations, and PlatformIO library metadata. These files are build
   inputs, not hand-maintained protocol definitions.
5. [`zcbor-requirements.txt`](zcbor-requirements.txt) records the manually
   installed generator version used to refresh the checked-in codecs.

## Updating the protocol

Install the generator dependencies manually, then regenerate in this order:

```sh
python3 -m pip install --force-reinstall -r protocol/zcbor-requirements.txt
scripts/generate_flova_link_cbor.sh
python3 scripts/generate_flova_link_vectors.py
scripts/check_flova_link_contract.sh
cmake -S . -B /tmp/flova-core-build
cmake --build /tmp/flova-core-build
ctest --test-dir /tmp/flova-core-build --output-on-failure
```

Review both the schema and generated diff. Do not edit files below
`generated/` directly, and do not modify vendored zcbor under `third_party/` as
part of a protocol change. A wire-compatible change still requires explicit
review of the CDDL, vectors, generated codecs, and native tests.

## Runtime boundary

The generated codec is the only firmware protocol implementation. The
portable SDK owns bounded message values and Link-facing contracts; Arduino
and board packages own WebSocket, TLS, Wi-Fi, storage, and callback scheduling.
The wire contract does not permit a second JSON, TLV, or generic CBOR tree
codec alongside the generated implementation.

The current profile uses complete frames no larger than 512 bytes, with a
fixed 12-byte frame header and bounded CBOR nesting. ESP8266 Link transport
uses the verified 2,048-byte receive and 512-byte transmit TLS buffers; OTA
uses its separate HTTPS profile.
