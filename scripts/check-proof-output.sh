#!/usr/bin/env bash
# Verify that two concurrent prove processes can write distinct proof paths.
# Run this on the test host; it requires a built Groth16 binary and an existing circuit.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
G16="${G16_BIN_DIR:-$ROOT/build/groth16/bin}"
DATA="${G16_DATA_DIR:-$ROOT/build/groth16/data}"
STEM="${1:-matmul_10}"
TIMEOUT_BIN="${TIMEOUT_BIN:-timeout}"

if [[ ! -x "$G16/groth16-setup" || ! -x "$G16/groth16-prove" || ! -x "$G16/groth16-verify" ]]; then
  echo "skip: groth16 binaries not found under $G16"
  exit 0
fi
if [[ ! -f "$DATA/$STEM.bin" ]]; then
  echo "skip: $DATA/$STEM.bin not found" >&2
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$G16/groth16-setup" --backend cpu --path "$DATA" "$STEM.bin" >/dev/null
"$G16/groth16-prove" --backend cpu --path "$DATA" --output "$TMP/p0.proof.bin" "$STEM.bin" >/dev/null &
P0=$!
"$G16/groth16-prove" --backend cpu --path "$DATA" --output "$TMP/p1.proof.bin" "$STEM.bin" >/dev/null &
P1=$!
wait "$P0"
wait "$P1"

for proof in "$TMP/p0.proof.bin" "$TMP/p1.proof.bin"; do
  [[ -s "$proof" ]] || { echo "missing proof: $proof" >&2; exit 1; }
  "$TIMEOUT_BIN" 300 "$G16/groth16-verify" --path "$DATA" --proof "$proof" "$STEM.bin"
done

echo "check-proof-output: ok"
