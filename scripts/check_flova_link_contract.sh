#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1:-all}
case "$mode" in
  all|--schema-only) ;;
  *) echo "usage: $0 [--schema-only]" >&2; exit 2 ;;
esac

"$repo_dir/scripts/lint_flova_link_cddl.sh"
if [ "$mode" = all ]; then
  "$repo_dir/scripts/check_flova_link_hot_path.sh"
  "$repo_dir/scripts/check_passive_esp_ownership.sh"
  # BearSSL selects IRAM for record buffers internally; contexts belong in DRAM.
  if rg -n 'HeapSelectIram' "$repo_dir/packages/flova-esp8266/include/FlovaEsp8266Platform.h"; then
    echo "ESP8266 transport must not move TLS contexts into the record-buffer heap" >&2
    exit 1
  fi
  for source in common encode decode print; do
    rg -F '#define ZCBOR_CANONICAL' "$repo_dir/third_party/zcbor/flova/zcbor_$source.c" >/dev/null
  done
fi
echo "Flova Link contract checks passed"
