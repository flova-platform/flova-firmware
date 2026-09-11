#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version_header="$repo_dir/packages/flova-arduino/include/FlovaSdkVersion.h"

version=$(sed -n 's/^#define FLOVA_VERSION "\([^"]*\)"/\1/p' "$version_header")
[ -n "$version" ] || {
  echo "canonical Flova version is missing" >&2
  exit 1
}

grep -F -q '#define FLOVA_SDK_VERSION FLOVA_VERSION' "$version_header" || {
  echo "SDK version is not derived from canonical Flova version" >&2
  exit 1
}
grep -F -q '#define FLOVA_FIRMWARE_VERSION FLOVA_VERSION' \
  "$repo_dir/packages/flova-arduino/include/FlovaArduino.h" || {
  echo "firmware version is not derived from canonical Flova version" >&2
  exit 1
}

for manifest in \
  packages/flova-arduino/library.json \
  packages/flova-embedded-sdk/library.json \
  packages/flova-esp32/library.json \
  packages/flova-esp8266/library.json \
  packages/flova-sdk-release/library.json; do
  actual=$(sed -n 's/^[[:space:]]*"version": "\([^"]*\)",/\1/p' "$repo_dir/$manifest" | head -n 1)
  [ "$actual" = "$version" ] || {
    echo "$manifest version does not match $version: $actual" >&2
    exit 1
  }
done

for properties in \
  packages/flova-embedded-sdk/library.properties \
  packages/flova-sdk-release/library.properties; do
  actual=$(sed -n 's/^version=//p' "$repo_dir/$properties")
  [ "$actual" = "$version" ] || {
    echo "$properties version does not match $version: $actual" >&2
    exit 1
  }
done

grep -F -q "\"Flova Embedded SDK\": \"^$version\"" \
  "$repo_dir/packages/flova-arduino/library.json" || {
  echo "Arduino package dependency does not match $version" >&2
  exit 1
}
for manifest in packages/flova-esp32/library.json packages/flova-esp8266/library.json; do
  grep -F -q "\"Flova Arduino\": \"^$version\"" "$repo_dir/$manifest" || {
    echo "$manifest dependency does not match $version" >&2
    exit 1
  }
done

for target in esp32 esp8266; do
  grep -F -q "flova-platform/FlovaSDK@^$version" \
    "$repo_dir/packages/flova-sdk-release/extras/platformio/$target/platformio.ini" || {
    echo "exported PlatformIO example does not match $version: $target" >&2
    exit 1
  }
done

if grep -F -q -- '-DFLOVA_FIRMWARE_VERSION=' "$repo_dir/platformio.ini"; then
  echo "official PlatformIO builds must use the canonical Flova version" >&2
  exit 1
fi

printf 'Flova version consistent: %s\n' "$version"
