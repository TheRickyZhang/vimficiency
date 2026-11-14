#!/usr/bin/env bash
# CI restores build/ from actions/cache so Ninja rebuilds incrementally, but
# checkout stamps every source with the current time. `mark` ages all tracked
# files, then re-touches only those whose blob differs from the index recorded
# alongside the restored build. Commit times are not used: a commit authored
# before the cached build but pushed after it would look up to date.
set -euo pipefail

index=build/.ci-source-index

case "${1:-}" in
  mark)
    [[ -f $index ]] || exit 0
    git ls-files -z | xargs -0 touch -c -h -d @1
    { git ls-files -s | grep -vxFf "$index" || true; } | cut -f2 | tr '\n' '\0' | xargs -0 -r touch -c -h
    ;;
  record)
    git ls-files -s > "$index"
    ;;
  *)
    echo "usage: $0 mark|record" >&2
    exit 2
    ;;
esac
