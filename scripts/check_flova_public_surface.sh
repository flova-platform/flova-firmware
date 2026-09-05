#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

failed=0

if find packages -type f \( -name 'Flova.h' -o -name 'FlovaCore.h' \) | grep -q .; then
  echo "error: removed compatibility headers are still present" >&2
  failed=1
fi

if find packages -type f -name 'ArduinoFlovaApplicationHardware.h' | grep -q .; then
  echo "error: custom-application pin-mapping adapter is still present" >&2
  failed=1
fi

custom_facades="packages/flova-esp32/include/FlovaEsp32.h packages/flova-esp32/include/FlovaEsp32Ble.h packages/flova-esp8266/include/FlovaEsp8266.h"
if rg -n 'digitalOutput\(' $custom_facades; then
  echo "error: custom SDK facades must not register application pins" >&2
  failed=1
fi

if rg -n 'FlovaCore\.h|Flova\.h|Flova Device SDK|flova-device-sdk' \
    README.md AGENTS.md packages examples test CMakeLists.txt platformio.ini; then
  echo "error: removed public names remain in repository guidance or consumers" >&2
  failed=1
fi

normal_examples="examples/custom-arduino-client examples/custom-arduino-provisioning examples/datastream-api-esp32 examples/datastream-api-esp8266 examples/universal-esp32 examples/universal-esp8266 examples/custom-board-basic"
if rg -n '#include[[:space:]]*[<"](FlovaConfiguration|FlovaClientLink|FlovaProvisioningAdapter|FlovaWifiProvisioning|FlovaEsp(32|8266)(Provisioning|Services)|adapters/)' $normal_examples; then
  echo "error: internal Flova adapter headers leaked into a normal example" >&2
  failed=1
fi

if rg -n '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)\b' examples -g '*.cpp' -g '*.h'; then
  echo "error: examples must use explicit targets instead of conditional compilation" >&2
  failed=1
fi

generic_arduino_headers="packages/flova-arduino/include"
if rg -n 'ESP32|ESP8266|ESP\.|BearSSL|WiFiClientSecure|ESP8266HTTPClient|HTTPClient\.h|Update\.h|Updater\.h|esp_system\.h|user_interface\.h|HeapSelect|FlovaTlsProfile|ArduinoOtaInstaller' "$generic_arduino_headers" -g '*.h'; then
  echo "error: board-specific ESP transport or OTA code leaked into Flova Arduino" >&2
  failed=1
fi

provisioning_headers="packages/flova-arduino/include/FlovaProvisioningAdapter.h packages/flova-esp32/include/FlovaEsp32Provisioning.h packages/flova-esp32/include/FlovaEsp32BleProvisioning.h packages/flova-esp8266/include/FlovaEsp8266Provisioning.h"
if rg -n 'beginRuntime|runtimeConnected|clockReady|defaultHardwareId|defaultFirmwareTarget|ArduinoFlovaUtcBootstrap|WiFi\.begin' $provisioning_headers; then
  echo "error: provisioning adapters own runtime network, TLS clock, or identity behavior" >&2
  failed=1
fi

runtime_headers="packages/flova-arduino/include/FlovaRuntimeServices.h packages/flova-esp32/include/FlovaEsp32Services.h packages/flova-esp8266/include/FlovaEsp8266Services.h"
if rg -n 'startProvisioning|stopProvisioning|requiresRestartBeforeProvisioning' $runtime_headers; then
  echo "error: runtime services own setup-channel behavior" >&2
  failed=1
fi

[ "$failed" -eq 0 ] || exit 1
echo "Flova public-surface checks passed"
