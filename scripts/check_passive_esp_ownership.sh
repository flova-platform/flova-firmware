#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

paths="packages/flova-esp32/include/FlovaEsp32.h packages/flova-esp8266/include/FlovaEsp8266.h"
pattern='WiFi\.(begin|mode|disconnect|softAP)|server\.(begin|stop)|ESP\.(restart|reboot)|\b(configTime|pinMode|digitalWrite|analogWrite)[[:space:]]*\('

if rg -n -e "$pattern" $paths; then
  echo "error: passive ESP SDK facade must not own network, server, GPIO, clock policy, or restart" >&2
  exit 1
fi

# FlovaClient is the single owner of private storage startup. Keeping startup
# out of board facades prevents direct SDK compositions from silently using an
# unmounted filesystem, while also preventing board wrappers from mounting it
# twice.
storage_starts=$(rg -o 'storage_\.begin\(\)' packages/flova-arduino/include/Flova.h | wc -l | tr -d ' ')
if [ "$storage_starts" -ne 1 ]; then
  echo "error: FlovaClient must initialize storage exactly once" >&2
  exit 1
fi

storage_wrappers="packages/flova-esp32/include/FlovaEsp32.h packages/flova-esp32/include/FlovaUniversalEsp32.h packages/flova-esp8266/include/FlovaEsp8266.h packages/flova-esp8266/include/FlovaUniversalEsp8266.h"
if rg -n -e 'storage_\.begin\(\)|beginStorage\(' $storage_wrappers; then
  echo "error: board facades must delegate private storage startup to FlovaClient" >&2
  exit 1
fi

echo "Passive ESP ownership checks passed"
