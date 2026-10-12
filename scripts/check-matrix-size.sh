#!/usr/bin/env bash
set -euo pipefail

size=${1:-}
safe_max=${2:-16}

if [[ ! "$size" =~ ^[1-9][0-9]*$ ]]; then
  echo "matrix size must be a positive integer" >&2
  exit 2
fi

if (( size > safe_max )) && [[ ${NKR_ZKP_ALLOW_LARGE:-0} != 1 ]]; then
  echo "refusing matrix size ${size}; safe default maximum is ${safe_max}" >&2
  echo "set NKR_ZKP_ALLOW_LARGE=1 only after checking RAM/VRAM estimates" >&2
  exit 75
fi

