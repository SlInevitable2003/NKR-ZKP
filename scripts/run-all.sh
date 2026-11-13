#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
for suite in functional p1 p2; do
  python3 -m scripts.bench run --suite "$suite" "$@"
done
