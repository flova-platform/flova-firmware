#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work_dir=${1:?usage: scripts/check_sdk_release.sh WORK_DIRECTORY}
sdk_dir="$work_dir/FlovaSDK"

mkdir -p "$work_dir"
scripts="$repo_dir/scripts"

"$scripts/export_sdk_release.sh" "$sdk_dir"
sdk_version=$(sed -n 's/^[[:space:]]*"version": "\([^"]*\)",/\1/p' "$sdk_dir/library.json" | head -n 1)
for target in esp32 esp8266; do
  grep -F -q "lib_deps = flova-platform/FlovaSDK@^$sdk_version" \
    "$sdk_dir/extras/platformio/$target/platformio.ini" || {
    echo "PlatformIO example version does not match SDK version: $target" >&2
    exit 1
  }
done
grep -F -q -- '-DPIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED' \
  "$sdk_dir/extras/platformio/esp8266/platformio.ini" || {
  echo "ESP8266 PlatformIO example is missing the IRAM heap profile" >&2
  exit 1
}
pio pkg pack "$sdk_dir" -o "$work_dir/FlovaSDK.tar.gz"

for target in esp32 esp8266; do
  project_dir="$work_dir/$target"
  mkdir -p "$project_dir"
  cp -R "$sdk_dir/extras/platformio/$target/." "$project_dir/"
  sed -i "s|flova-platform/FlovaSDK@[^[:space:]]*|file://$work_dir/FlovaSDK.tar.gz|" \
    "$project_dir/platformio.ini"
  pio run --project-dir "$project_dir"
done

test -s "$work_dir/FlovaSDK.tar.gz"
printf 'SDK package validated: %s\n' "$work_dir/FlovaSDK.tar.gz"
