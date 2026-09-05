# SDK release

The public `FlovaSDK` package is generated from this monorepo and published to
both PlatformIO and Arduino Library Manager. Never edit the generated release
tree directly.

## Build the release tree

Update both versions in `packages/flova-sdk-release`, regenerate protocol
artifacts when the CDDL changes, then run:

```sh
scripts/export_sdk_release.sh /tmp/FlovaSDK
arduino-lint --library-manager submit /tmp/FlovaSDK
pio pkg pack /tmp/FlovaSDK -o /tmp/FlovaSDK.tar.gz
```

Compile the Arduino ESP32 example and both PlatformIO examples from this clean
tree before publishing. Run the complete repository validation matrix as well.

## PlatformIO

Confirm the authenticated registry owner before publishing. Package versions
cannot be reused, even after unpublishing.

```sh
pio account show
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
git -C /tmp/FlovaSDK init -b library-release
git -C /tmp/FlovaSDK remote add origin https://github.com/flova-platform/flova-firmware.git
git -C /tmp/FlovaSDK add .
git -C /tmp/FlovaSDK commit -m "release: FlovaSDK 0.2.0"
git -C /tmp/FlovaSDK push origin HEAD:library-release
git -C /tmp/FlovaSDK tag -a v0.2.0 -m "FlovaSDK 0.2.0"
git -C /tmp/FlovaSDK push origin v0.2.0
```

Submit `https://github.com/flova-platform/flova-firmware` to the Arduino
Library Registry. Arduino Library Manager advertises ESP32 support only.
ESP8266 remains available through PlatformIO because its bounded cooperative
transport requires a pinned framework preparation script.

Do not move or replace a published version tag. Release a new version for any
correction.
