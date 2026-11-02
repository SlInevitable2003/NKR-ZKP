#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

paths=(
  operators/hip/include
  protocols/groth16/backends/hip/include
  protocols/groth16/backends/hip/src
  protocols/groth16/backends/hip/test
  protocols/plonk/backends/hip/src
  protocols/plonk/backends/hip/test
)

pattern='__shfl|shfl_(up|down|xor)|__ballot|__syncwarp|warpSize|cooperative_groups|hip_cooperative_groups|cg::|hipLaunchCooperativeKernel'
if rg -n "$pattern" "${paths[@]}"; then
  echo "DCU safety check failed: active HIP code uses a forbidden warp/cooperative primitive" >&2
  exit 1
fi

echo "HIP/DCU safety: no warp-local or cooperative-group dependency"
