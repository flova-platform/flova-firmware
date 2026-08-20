#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

failed=0

if find packages -type f \( -name 'Flova.h' -o -name 'FlovaCore.h' \) | grep -q .; then
  echo "error: removed compatibility headers are still present" >&2
  failed=1
fi

if rg -n 'FlovaCore\.h|Flova\.h|Flova Device SDK|flova-device-sdk' \
    README.md AGENTS.md packages .docs examples test CMakeLists.txt platformio.ini; then
  echo "error: removed public names remain in repository guidance or consumers" >&2
  failed=1
fi

normal_examples="examples/custom-arduino-client examples/custom-arduino-provisioning examples/datastream-api-esp32 examples/datastream-api-esp8266 examples/universal-esp32 examples/universal-esp8266 examples/custom-board-basic"
if rg -n '#include[[:space:]]*[<"](FlovaConfiguration|FlovaClientLink|FlovaProvisioningAdapter|FlovaWifiProvisioning|FlovaEsp(32|8266)(Provisioning|Services)|adapters/)' $normal_examples; then
  echo "error: internal Flova adapter headers leaked into a normal example" >&2
  failed=1
fi

[ "$failed" -eq 0 ] || exit 1
echo "Flova public-surface checks passed"
