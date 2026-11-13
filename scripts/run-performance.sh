#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SUITE="${SUITE:-p1}"
exec python3 -m scripts.bench run --suite "$SUITE" "$@"
