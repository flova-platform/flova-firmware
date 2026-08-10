#!/usr/bin/env sh
# Run both static Flova Link v1 contract checks.  Keep this stable entry point
# for local and CI callers; each check is also independently runnable.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1:-all}

case "$mode" in
  all|--schema-only) ;;
  *)
    echo "usage: $0 [--schema-only]" >&2
    exit 2
    ;;
esac

"$repo_dir/scripts/lint_flova_link_cddl.sh"
if [ "$mode" = "all" ]; then
  "$repo_dir/scripts/check_flova_link_hot_path.sh"
fi

echo "Flova Link contract checks passed"
