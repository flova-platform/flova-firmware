#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
generated_dir="$repo_dir/protocol/generated/firmware"
generator="$repo_dir/third_party/zcbor/zcbor/zcbor.py"
schema="$repo_dir/protocol/flova-link-v1.cddl"

python3 "$generator" code \
  --decode --encode \
  --default-max-qty 1 \
  --default-bit-size 64 \
  --cddl "$schema" \
  --entry-types \
  auth auth-ok auth-error ping pong bootstrap-auth bootstrap-committed \
  bootstrap-error heartbeat state command-result config-reported \
  ota-reported schedule-reported schedule-renew time-request config-ack \
  command ingestion-ack config-desired ota-desired schedule-desired \
  time-response flow-control message-rejected config-begin config-record \
  config-end schedule-record-message schedule-end error \
  --output-c "$generated_dir/src/flova_link.c" \
  --output-h "$generated_dir/include/flova_link.h" \
  --output-h-types "$generated_dir/include/flova_link_types.h"
