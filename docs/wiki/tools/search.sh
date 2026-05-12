#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <query>" >&2
    exit 2
fi

cd "$(dirname "$0")/.."
rg -n --glob '*.md' "$1" .
