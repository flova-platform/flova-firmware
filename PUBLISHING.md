# SDK release

The public `FlovaSDK` package is generated from this monorepo and published to
both PlatformIO and Arduino Library Manager. Never edit the generated release
tree directly.

## Build and validate the release tree

Update `library.json`, `library.properties`, and both packaged PlatformIO
example dependency versions in `packages/flova-sdk-release`, regenerate
protocol artifacts when the CDDL changes, then run:

```sh
scripts/export_sdk_release.sh /tmp/FlovaSDK
pio pkg pack /tmp/FlovaSDK -o /tmp/FlovaSDK.tar.gz
scripts/check_sdk_release.sh /tmp/flova-sdk-check
```

The check script compiles both PlatformIO examples against the exported package.
The SDK workflow also compiles the Arduino ESP32 example and runs the complete
repository validation matrix before publishing.

## PlatformIO

Package versions cannot be reused, even after unpublishing. The automated
release workflow authenticates with the `PLATFORMIO_AUTH_TOKEN` repository
secret.

```sh
pio pkg publish /tmp/FlovaSDK.tar.gz --owner flova-platform
```

Verify a clean install with `flova-platform/FlovaSDK@^0.2.0` on ESP32 and ESP8266.
The ESP8266 project must retain the packaged `extra_scripts` entry and MMU build
flag shown in `extras/platformio/esp8266/platformio.ini`.

## Arduino Library Manager

Arduino Library Manager requires the release files at repository root. Keep
`main` as the canonical monorepo and publish the generated tree through the
`library-release` branch. Version tags must point to commits on that branch:

```sh
Push `sdk-vX.Y.Z` on `main`. The SDK workflow validates that the tag version
matches both package manifests, publishes PlatformIO, replaces the generated
`library-release` contents, and creates the immutable Arduino tag `vX.Y.Z`.
```

Submit `https://github.com/flova-platform/flova-firmware` to the Arduino
Library Registry. Arduino Library Manager advertises ESP32 support only.
ESP8266 remains available through PlatformIO because its bounded cooperative
transport requires a pinned framework preparation script.

Do not move or replace a published version tag. Release a new version for any
correction. Existing firmware binary releases are historical only; new device
projects build and upload from the SDK.
