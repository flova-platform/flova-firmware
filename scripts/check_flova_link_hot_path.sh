#!/usr/bin/env sh
# Reject legacy and allocating APIs in code that carries or installs Link data.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

allowlist="scripts/flova-link-hot-path.allowlist"
rg_bin=${RG:-rg}
failed=0
hot_paths=

if ! command -v "$rg_bin" >/dev/null 2>&1; then
  echo "error: ripgrep is required for Flova Link hot-path checks" >&2
  exit 2
fi
if [ ! -f "$allowlist" ]; then
  echo "error: missing Flova Link hot-path allowlist" >&2
  exit 2
fi

add_hot_path() {
  if [ -e "$1" ]; then
    hot_paths="$hot_paths $1"
  fi
}

# Keep the scope explicit. A deleted legacy file disappears from the list
# automatically; an allowlist entry must never preserve it.
add_hot_path packages/flova-device-sdk/include/FlovaLinkCodec.h
add_hot_path packages/flova-device-sdk/include/FlovaLinkCbor.h
add_hot_path packages/flova-device-sdk/include/FlovaConfigurationInstaller.h
add_hot_path packages/flova-device-sdk/include/FlovaTransport.h
add_hot_path packages/flova-arduino/include/FlovaConfiguration.h
add_hot_path packages/flova-arduino/include/FlovaLinkValueDecoder.h
add_hot_path packages/flova-arduino/include/adapters/ArduinoDeviceLink.h
add_hot_path packages/flova-esp8266/include/FlovaEsp8266.h
add_hot_path packages/flova-esp32/include/FlovaEsp32.h
add_hot_path protocol/generated/firmware

if [ -z "$hot_paths" ]; then
  echo "error: no Flova Link/configuration hot paths found" >&2
  exit 2
fi

is_allowlisted() {
  allow_path=$1
  allow_line=$2
  allow_rule=$3
  awk -F: -v path="$allow_path" -v line="$allow_line" -v rule="$allow_rule" '
    /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
    $1 == path && $2 == line && $3 == rule { found = 1 }
    END { exit(found ? 0 : 1) }
  ' "$allowlist"
}

scan_hot_path() {
  rule=$1
  pattern=$2
  output_file=$(mktemp "${TMPDIR:-/tmp}/flova-link-hot-path.XXXXXX")
  trap 'rm -f "$output_file"' EXIT HUP INT TERM

  if "$rg_bin" --no-heading -n -e "$pattern" $hot_paths >"$output_file"; then
    while IFS= read -r hit; do
      hit_path=${hit%%:*}
      hit_rest=${hit#*:}
      hit_line=${hit_rest%%:*}
      if ! is_allowlisted "$hit_path" "$hit_line" "$rule"; then
        printf '%s\n' "error: Flova Link hot path forbids $rule: $hit" >&2
        failed=1
      fi
    done <"$output_file"
  fi
  rm -f "$output_file"
  trap - EXIT HUP INT TERM
}

scan_hot_path dynamic_json 'DynamicJsonDocument|StaticJsonDocument|JsonDocument|ArduinoJson|deserializeJson|serializeJson'
scan_hot_path runtime_json 'runtimeJson'
scan_hot_path heap_allocation '\b(malloc|calloc|realloc|free)[[:space:]]*\(|\b(new|delete)\b'
scan_hot_path dynamic_container 'std::(vector|map)|\b(vector|map)<'
scan_hot_path generic_cbor_tree 'CborValue|CborParser|nlohmann'
scan_hot_path legacy_tlv '\bTLV\b'
scan_hot_path legacy_value_decoder 'FlovaLinkValueDecoder|value[ _-]*decoder'
scan_hot_path unconditional_yield '(^|[^_[:alnum:]])yield[[:space:]]*\('

if [ "$failed" -ne 0 ]; then
  exit 1
fi
echo "Flova Link hot-path checks passed"
