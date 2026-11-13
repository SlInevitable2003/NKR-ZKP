#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ "${1:-}" == "--aggregate" ]]; then
  shift
  exec python3 -m scripts.bench aggregate "$@"
fi

PLATFORM=""
case "${1:-}" in
  gpu) PLATFORM=cuda; shift ;;
  dcu) PLATFORM=hip; shift ;;
esac

if [[ "${1:-}" == "--aggregate" ]]; then
  shift
  exec python3 -m scripts.bench aggregate "$@"
fi
if [[ -z "$PLATFORM" ]]; then
  if nvidia-smi -L >/dev/null 2>&1; then
    PLATFORM=cuda
  elif command -v rocm-smi >/dev/null 2>&1 || command -v amd-smi >/dev/null 2>&1; then
    PLATFORM=hip
  else
    echo "distributed requires a GPU (cuda) or DCU (hip) host" >&2
    exit 1
  fi
fi

if [[ "$PLATFORM" == "cuda" ]]; then
  DEPLOYMENT=cuda_3x4
else
  DEPLOYMENT=hip_2x8
fi
ARGS=()
if [[ " $* " != *" --deployment "* ]]; then
  ARGS+=(--deployment "$DEPLOYMENT")
fi
exec python3 -m scripts.bench run-node --platform "$PLATFORM" "${ARGS[@]}" "$@"
