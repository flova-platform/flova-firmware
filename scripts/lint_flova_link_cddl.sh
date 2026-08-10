#!/usr/bin/env sh
# Enforce the deliberately small, non-extensible CDDL profile used by Link v1.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
schema=${1:-"$repo_dir/protocol/flova-link-v1.cddl"}
rg_bin=${RG:-rg}
failed=0

if ! command -v "$rg_bin" >/dev/null 2>&1; then
  echo "error: ripgrep is required for Flova Link CDDL lint" >&2
  exit 2
fi
if [ ! -f "$schema" ]; then
  echo "error: missing Flova Link schema: $schema" >&2
  exit 2
fi

if ! "$rg_bin" -q '^; Maximum CBOR nesting depth: 4$' "$schema"; then
  echo "error: $schema must declare '; Maximum CBOR nesting depth: 4'" >&2
  failed=1
fi

for forbidden in \
  'any|\bany\b' \
  'tag|#6|#[0-9]|\.cbor\b|\btagged?\b' \
  'indefinite|\bindefinite\b|\bindef\b' \
  'unbounded-choice|//'; do
  rule=${forbidden%%|*}
  pattern=${forbidden#*|}
  if "$rg_bin" -n -e "$pattern" "$schema"; then
    echo "error: $schema permits forbidden CDDL construct ($rule)" >&2
    failed=1
  fi
done

# `1*32 item` is bounded; `* item` and `1* item` are not.
if "$rg_bin" -n -e '(^|[^0-9])[0-9]*\*[[:space:]]*[A-Za-z_]' "$schema"; then
  echo "error: $schema contains an unbounded CDDL occurrence" >&2
  failed=1
fi

if awk '
  /^[[:space:]]*;/ { next }
  /(^|[^[:alnum:]_])(tstr|bstr)([^[:alnum:]_]|$)/ && $0 !~ /\.size/ {
    printf "%s:%d: unbounded text or byte string\n", FILENAME, FNR
    failed = 1
  }
  END { exit(failed ? 1 : 0) }
' "$schema"; then :; else
  failed=1
fi

# Reject direct and indirect type recursion. The schema is deliberately closed,
# therefore this compact parser need only understand named type definitions.
if awk '
  function finish() { if (current != "") definition[current] = body }
  function has_word(text, word) {
    return match(text, "(^|[^[:alnum:]-])" word "([^[:alnum:]-]|$)")
  }
  function visit(node,    names, count, i, child) {
    if (state[node] == 1) { print "recursive CDDL type: " node > "/dev/stderr"; recursive = 1; return }
    if (state[node] == 2) return
    state[node] = 1
    count = split(edge[node], names, " ")
    for (i = 1; i <= count; i++) {
      child = names[i]
      if (child != "") visit(child)
    }
    state[node] = 2
  }
  {
    line = $0
    sub(/;.*/, "", line)
    if (match(line, /^[[:space:]]*[A-Za-z][A-Za-z0-9-]*[[:space:]]*=/)) {
      finish()
      current = line
      sub(/^[[:space:]]*/, "", current)
      sub(/[[:space:]]*=.*/, "", current)
      body = line
      sub(/^[^=]*=/, "", body)
      next
    }
    if (current != "") body = body " " line
  }
  END {
    finish()
    for (source in definition)
      for (target in definition)
        if (source != target && has_word(definition[source], target))
          edge[source] = edge[source] " " target
    for (source in definition) visit(source)
    exit(recursive ? 1 : 0)
  }
' "$schema"; then :; else
  failed=1
fi

if [ "$failed" -ne 0 ]; then
  exit 1
fi
echo "Flova Link CDDL lint passed"
