#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root_dir"

required=(
  protocols/groth16/common
  protocols/groth16/app
  protocols/groth16/backends/cpu
  protocols/groth16/backends/cuda
  protocols/groth16/backends/dcu
  protocols/groth16/third_party/depends/libsnark/CMakeLists.txt
  protocols/groth16/third_party/depends/cgbn/cgbn.h
  protocols/plonk/implementations/cpu/Cargo.toml
  protocols/plonk/implementations/cuda/Cargo.toml
  protocols/stark/winterfell/Cargo.toml
)

for path in "${required[@]}"; do
  if [[ ! -e "$path" ]]; then
    echo "missing required path: $path" >&2
    exit 1
  fi
done

if find protocols -type d -name .git -print -quit | grep -q .; then
  echo "nested Git directory found under protocols/" >&2
  exit 1
fi

if find protocols -name .gitmodules -o -name .git | grep -q .; then
  echo "nested Git metadata found under protocols/" >&2
  exit 1
fi

if git ls-files protocols | grep -Eq '(^|/)(build|target)/|\.prehip$|\.bin$'; then
  echo "tracked build output, backup, or generated binary found under protocols/" >&2
  exit 1
fi

echo "repository layout: ok"
