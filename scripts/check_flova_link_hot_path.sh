#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

paths="
packages/flova-device-sdk/include/FlovaCore.h
packages/flova-device-sdk/include/FlovaLinkMessages.h
packages/flova-device-sdk/include/FlovaConfigurationInstaller.h
packages/flova-arduino/include/Flova.h
packages/flova-arduino/include/FlovaLinkConfigurationStorage.h
packages/flova-arduino/include/adapters/ArduinoDeviceLink.h
protocol/generated/firmware
"

failed=0
check() {
  label=$1
  pattern=$2
  if rg -n -e "$pattern" $paths; then
    echo "error: Link/configuration hot path forbids $label" >&2
    failed=1
  fi
}

check dynamic_json 'DynamicJsonDocument|JsonDocument|ArduinoJson|deserializeJson|serializeJson'
check heap_allocation '\b(malloc|calloc|realloc|free)[[:space:]]*\(|\b(new|delete)\b'
check dynamic_container 'std::(vector|map|string)|\b(vector|map)<'
check generic_cbor_tree 'CborValue|CborParser|nlohmann'
check unconditional_yield '(^|[^_[:alnum:]])yield[[:space:]]*\('

[ "$failed" -eq 0 ] || exit 1
echo "Flova Link hot-path checks passed"
