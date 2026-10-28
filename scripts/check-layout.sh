#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root_dir"

required=(
  protocols/groth16/common
  protocols/groth16/app
  protocols/groth16/backends/cpu
  protocols/groth16/backends/cuda
  protocols/groth16/backends/hip
  protocols/plonk/common/include/plonk/api.hpp
  protocols/plonk/common/include/plonkish/arithmetization.hpp
  protocols/plonk/backends/cpu
  protocols/plonk/backends/cuda
  protocols/plonk/backends/hip
  operators/cuda/include/nkr/operators/cuda.hpp
  operators/hip/include/nkr/operators/hip.hpp
  third_party/lightzk/depends/libsnark/CMakeLists.txt
  third_party/lightzk/depends/cgbn/cgbn.h
  protocols/stark/winterfell/Cargo.toml
)

for path in "${required[@]}"; do
  if [[ ! -e "$path" ]]; then
    echo "missing required path: $path" >&2
    exit 1
  fi
done

empty_source_dir=$(find common operators protocols -type d -empty -print -quit)
if [[ -n "$empty_source_dir" ]]; then
  echo "empty directory found under source roots: $empty_source_dir" >&2
  exit 1
fi

if find protocols operators third_party -type d -name .git -print -quit | grep -q .; then
  echo "nested Git directory found under source roots" >&2
  exit 1
fi

if find protocols operators third_party \( -name .gitmodules -o -name .git \) | grep -q .; then
  echo "nested Git metadata found under source roots" >&2
  exit 1
fi

if git ls-files protocols operators third_party | grep -Eq '(^|/)(build|target)/|\.prehip$|\.bin$'; then
  echo "tracked build output, backup, or generated binary found under source roots" >&2
  exit 1
fi

echo "repository layout: ok"
