#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
failed=0

if rg -n '#include[[:space:]]*[<"](Arduino|ESP|WiFi|FS|LittleFS)' \
    packages/flova-device-sdk/include; then
  echo "error: portable SDK includes a platform header" >&2
  failed=1
fi

if rg -n '\bString\b|std::(string|vector|map)|\b(new|delete)\b' \
    packages/flova-device-sdk/include; then
  echo "error: portable SDK uses an allocating or Arduino value type" >&2
  failed=1
fi

if rg -n '\b(dynamic_cast|typeid|throw)[[:space:]<(]' packages examples test; then
  echo "error: firmware code uses RTTI or exceptions" >&2
  failed=1
fi

if find packages/flova-device-sdk -type f \
    \( -name 'FlovaDevice.h' -o -name 'FlovaTransport.h' \
       -o -name 'FlovaTypes.h' -o -name 'FlovaDevice.cpp' \) | grep -q .; then
  echo "error: legacy device runtime returned" >&2
  failed=1
fi

if rg -n '#if.*defined\((ESP32|ESP8266)\)' \
    packages/flova-device-sdk packages/flova-arduino/include/Flova.h; then
  echo "error: board selection leaked into shared runtime code" >&2
  failed=1
fi

if rg -n 'class[[:space:]]+FlovaEsp(32|8266)[[:space:]]*:' \
    packages/flova-esp32 packages/flova-esp8266; then
  echo "error: board facade must use composition, not inheritance" >&2
  failed=1
fi

[ "$failed" -eq 0 ] || exit 1
echo "Flova layer checks passed"
