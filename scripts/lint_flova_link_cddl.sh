#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
schema=${1:-"$repo_dir/protocol/flova-link-v1.cddl"}
failed=0

command -v rg >/dev/null 2>&1 || {
  echo "error: ripgrep is required" >&2
  exit 2
}
[ -f "$schema" ] || {
  echo "error: missing schema: $schema" >&2
  exit 2
}

if ! rg -q '^; Maximum CBOR nesting depth: 4$' "$schema"; then
  echo "error: schema must declare maximum CBOR nesting depth 4" >&2
  failed=1
fi

if rg -n -e '\bany\b|#6|#[0-9]|\.cbor\b|\bindef(inite)?\b' "$schema"; then
  echo "error: schema contains a forbidden generic CBOR construct" >&2
  failed=1
fi

if rg -n -e '(^|[^0-9])[0-9]*\*[[:space:]]*[A-Za-z_]' "$schema"; then
  echo "error: schema contains an unbounded occurrence" >&2
  failed=1
fi

if ! awk '
  /^[[:space:]]*;/ { next }
  /(^|[^[:alnum:]_])(tstr|bstr)([^[:alnum:]_]|$)/ && $0 !~ /\.size/ {
    printf "%s:%d: unbounded text or byte string\n", FILENAME, FNR
    failed = 1
  }
  END { exit(failed ? 1 : 0) }
' "$schema"; then
  failed=1
fi

[ "$failed" -eq 0 ] || exit 1
echo "Flova Link CDDL lint passed"
