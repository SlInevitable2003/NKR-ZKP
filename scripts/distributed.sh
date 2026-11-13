#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [[ "${1:-}" == "--aggregate" ]]; then
  shift
  exec python3 -m scripts.bench aggregate "$@"
fi
exec python3 -m scripts.bench run-node "$@"
