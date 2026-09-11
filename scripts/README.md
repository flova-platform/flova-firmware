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

## Contract guards

```sh
scripts/check_flova_link_contract.sh
scripts/check_flova_layers.sh
scripts/check_passive_esp_ownership.sh
scripts/check_flova_public_surface.sh
scripts/check_esp8266_stack_usage.py
```

The Link guard rejects unbounded schema and allocating codec/configuration hot
paths. The layer guard keeps platform APIs and legacy runtimes out of the
portable SDK. The passive-ownership guard prevents the normal ESP facade from
taking over networking, servers, GPIO, clock policy, or reboot. The stack guard consumes PlatformIO `-fstack-usage` output from
`universal-esp8266` and fails when critical continuation-stack frames grow.
The public-surface guard prevents removed compatibility names and internal
adapter headers from leaking into normal examples.

## SDK release validation

Check that the SDK, firmware, package manifests, dependencies, and exported
examples all use the same canonical Flova release version:

```sh
sh scripts/check_flova_version.sh
```

Export and compile the self-contained SDK package in a temporary directory:

```sh
scripts/check_sdk_release.sh /tmp/flova-sdk-check
```

The check builds both exported PlatformIO examples against the local package.
Arduino IDE supports both targets using stock cores.

Python bytecode, caches, build output, and PlatformIO output are ignored by
the repository and must not be committed.

## ESP8266 framework integration

ESP8266 uses public stock BearSSL APIs. Package validation must use an
unmodified framework installation; an old patched local cache is not evidence
of compatibility. Shared IRAM is optional; the default exported project uses DRAM.

## SDK release

`scripts/export_sdk_release.sh` creates the single self-contained `FlovaSDK`
package consumed by PlatformIO and Arduino Library Manager. The generated tree
contains release metadata, examples, public headers, board sources, generated
Link codecs, and zcbor with its upstream notices. Do not edit that generated
tree directly.
