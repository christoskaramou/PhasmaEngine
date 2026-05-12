#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

missing=0
for path in README.md index.md log.md tools/search.sh tools/lint.sh; do
    if [[ ! -f "$path" ]]; then
        echo "missing: $path" >&2
        missing=1
    fi
done

if [[ $missing -ne 0 ]]; then
    exit 1
fi

rg -n $'\t' --glob '*.md' . && {
    echo "tabs found in wiki markdown" >&2
    exit 1
}

echo "wiki lint ok"
