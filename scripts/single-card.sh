#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PLATFORM=""
case "${1:-}" in
  gpu) PLATFORM=cuda; shift ;;
  dcu) PLATFORM=hip; shift ;;
  cpu) PLATFORM=cpu; shift ;;
esac
if [[ -z "$PLATFORM" ]]; then
  if nvidia-smi -L >/dev/null 2>&1; then
    PLATFORM=cuda
  elif command -v rocm-smi >/dev/null 2>&1 || command -v amd-smi >/dev/null 2>&1; then
    PLATFORM=hip
  else
    PLATFORM=cpu
  fi
fi

exec python3 -m scripts.bench run --suite p1 --platform "$PLATFORM" "$@"
