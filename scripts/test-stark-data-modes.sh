#!/usr/bin/env bash
#SBATCH --job-name=stark-data-modes
#SBATCH --partition=debug
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:30:00

set -euo pipefail

cd "${SLURM_SUBMIT_DIR:?Submit this job from the repository root}"
export RAYON_NUM_THREADS=${SLURM_CPUS_PER_TASK:-4}
export CARGO_TARGET_DIR=${STARK_MODES_BUILD_DIR:-/tmp/stark-data-modes-${SLURM_JOB_ID}}
toolchain=${STARK_RUST_TOOLCHAIN:-stable}
matrix_size=${STARK_MATRIX_SIZE:-64} # this must be a power of 2 at least 2
fibonacci_length=${STARK_FIBONACCI_LENGTH:-32}

cd protocols/stark/winterfell
cargo "+$toolchain" build --release --features concurrent --package examples --bin winterfell

bin_dir="$CARGO_TARGET_DIR/release"
echo "STARK data mode test backend: cpu (Rayon); threads=$RAYON_NUM_THREADS"
for mode in dense sparse hybrid; do
    "$bin_dir/winterfell" matrix-mul -n "$matrix_size" --data-mode "$mode" --profile
    echo "PASS: matmul size=$matrix_size $mode cpu"
    "$bin_dir/winterfell" fib -n "$fibonacci_length" --data-mode "$mode" --profile
    echo "PASS: fibonacci length=$fibonacci_length $mode cpu"
done
echo "PASS: all STARK input data mode checks; backend=cpu"
