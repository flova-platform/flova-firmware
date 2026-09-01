#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

paths="
packages/flova-embedded-sdk/include/FlovaDevice.h
packages/flova-embedded-sdk/include/FlovaLinkMessages.h
packages/flova-embedded-sdk/include/FlovaConfigurationInstaller.h
packages/flova-arduino/include/FlovaArduino.h
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

if rg -n 'websocket_[.]handshake[[:space:]]*\(|[.]openLink[[:space:]]*\(' \
  packages/flova-arduino/include/adapters/ArduinoDeviceLink.h \
  packages/flova-esp32/include/FlovaEsp32Platform.h \
  packages/flova-esp8266/include/FlovaEsp8266Platform.h; then
  echo "error: normal Device Link connection must use cooperative start/poll states" >&2
  failed=1
fi

[ "$failed" -eq 0 ] || exit 1
echo "Flova Link hot-path checks passed"
