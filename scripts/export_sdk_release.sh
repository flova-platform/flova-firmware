#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir=${1:?usage: scripts/export_sdk_release.sh OUTPUT_DIRECTORY}

if [ -e "$output_dir" ]; then
  echo "output already exists: $output_dir" >&2
  exit 1
fi

mkdir -p "$output_dir/.github/workflows" "$output_dir/src/adapters" \
  "$output_dir/examples/Basic" \
  "$output_dir/extras/platformio/esp32/src" \
  "$output_dir/extras/platformio/esp8266/src" "$output_dir/scripts"

release_dir="$repo_dir/packages/flova-sdk-release"
properties_version=$(sed -n 's/^version=//p' "$release_dir/library.properties")
manifest_version=$(sed -n 's/^[[:space:]]*"version": "\([^"]*\)",/\1/p' "$release_dir/library.json")
if [ -z "$properties_version" ] || [ "$properties_version" != "$manifest_version" ]; then
  echo "release manifest versions do not match" >&2
  exit 1
fi

cp "$release_dir/library.properties" "$release_dir/library.json" "$output_dir/"
cp "$release_dir/README.md" "$output_dir/"
cp "$release_dir/src/FlovaSDK.h" "$output_dir/src/"
cp "$release_dir/examples/Basic/Basic.ino" "$output_dir/examples/Basic/"
cp "$release_dir/.github/workflows/arduino.yml" "$output_dir/.github/workflows/"
cp "$release_dir/extras/platformio/esp32/platformio.ini" "$output_dir/extras/platformio/esp32/"
cp "$release_dir/extras/platformio/esp32/src/main.cpp" "$output_dir/extras/platformio/esp32/src/"
cp "$release_dir/extras/platformio/esp8266/platformio.ini" "$output_dir/extras/platformio/esp8266/"
cp "$release_dir/extras/platformio/esp8266/src/main.cpp" "$output_dir/extras/platformio/esp8266/src/"
cp "$repo_dir/LICENSE" "$output_dir/"
cp "$repo_dir/third_party/zcbor/LICENSE" "$output_dir/extras/zcbor-LICENSE"
cp "$repo_dir/third_party/zcbor/UPSTREAM.md" "$output_dir/extras/zcbor-UPSTREAM.md"

cp "$repo_dir/packages/flova-embedded-sdk/include/"*.h "$output_dir/src/"
cp "$repo_dir/packages/flova-arduino/include/"*.h "$output_dir/src/"
cp "$repo_dir/packages/flova-arduino/include/adapters/"*.h "$output_dir/src/adapters/"
cp "$repo_dir/packages/flova-esp32/include/"*.h "$output_dir/src/"
cp "$repo_dir/packages/flova-esp32/src/"*.cpp "$output_dir/src/"
cp "$repo_dir/packages/flova-esp8266/include/"*.h "$output_dir/src/"
cp "$repo_dir/packages/flova-esp8266/scripts/patch_esp8266_bearssl_nonblocking.py" "$output_dir/scripts/"
cp "$repo_dir/protocol/generated/firmware/include/"*.h "$output_dir/src/"
cp "$repo_dir/protocol/generated/firmware/src/"*.c "$output_dir/src/"
cp "$repo_dir/third_party/zcbor/include/"*.h "$output_dir/src/"
cp "$repo_dir/third_party/zcbor/src/"*.c "$output_dir/src/"
