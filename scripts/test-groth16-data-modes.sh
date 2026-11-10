#!/usr/bin/env bash
#SBATCH --job-name=groth16-data-modes
#SBATCH --partition=debug
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G
#SBATCH --time=00:30:00

set -euo pipefail

cd "${SLURM_SUBMIT_DIR:?Submit this job from the repository root}"

conda_prefix=${CONDA_PREFIX:?Activate a Conda environment containing GMP before submission}
build_dir=${GROTH16_MODES_BUILD_DIR:-/tmp/groth16-data-modes-${SLURM_JOB_ID}}
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK:-8}
accelerator=${GROTH16_MODES_ACCELERATOR:-NONE}
backend=cpu
accelerator_flags=()
if [[ "$accelerator" == CUDA ]]; then
    module load "${CUDA_MODULE:-cuda/12.9}"
    accelerator_flags=(-DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCH:-90}")
    backend=cuda
elif [[ "$accelerator" == HIP ]]; then
    if [[ -n "${HIP_MODULE:-}" ]]; then module load "$HIP_MODULE"; fi
    if [[ -n "${HIP_ARCH:-}" ]]; then
        accelerator_flags=(-DCMAKE_HIP_ARCHITECTURES="$HIP_ARCH")
    fi
    backend=hip
elif [[ "$accelerator" != NONE ]]; then
    echo "Unsupported GROTH16_MODES_ACCELERATOR: $accelerator" >&2
    exit 1
fi
echo "Groth16 data mode test backend: $backend"

cmake -S . -B "$build_dir" \
    -DNKR_ZKP_PROTOCOL=GROTH16 -DNKR_ZKP_ACCELERATOR="$accelerator" "${accelerator_flags[@]}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -UNDEBUG" \
    -DGMP_INCLUDE_DIR="$conda_prefix/include" \
    -DGMP_LIBRARY="$conda_prefix/lib/libgmp.so" \
    -DGMP_LIBRARIES="$conda_prefix/lib/libgmp.so" \
    -DGMPXX_LIBRARIES="$conda_prefix/lib/libgmpxx.so"
cmake --build "$build_dir" --parallel "$OMP_NUM_THREADS" \
    --target groth16-gen groth16-setup groth16-prove groth16-verify

bin_dir="$build_dir/bin"
for mode in dense sparse hybrid; do
    # "$bin_dir/groth16-gen" --profile --data-mode "$mode" matmul 100
    # "$bin_dir/groth16-gen" --profile --data-mode "$mode" poseidon 2377
    # "$bin_dir/groth16-gen" --profile --data-mode "$mode" sql-aggregate 1024
    "$bin_dir/groth16-gen" --profile --data-mode "$mode" fibonacci 16
        
    # for circuit in matmul poseidon sql-aggregate fibonacci; do
    for circuit in fibonacci; do
        case "$circuit" in
            matmul)   n=100  ;;
            poseidon) n=2377 ;;
            sql-aggregate) n=1024 ;;
            fibonacci) n=16 ;;
        esac
        stem="${circuit//-/_}_$n"
        if [[ "$mode" != dense ]]; then stem="${stem}_${mode}"; fi
        "$bin_dir/groth16-setup" --profile --backend "$backend" "${stem}.bin"
        "$bin_dir/groth16-prove" --profile --backend "$backend" "${stem}.bin"
        "$bin_dir/groth16-verify" --profile "${stem}.bin"
        echo "PASS: $circuit $mode $backend"
    done
done
echo "PASS: all Groth16 input data mode checks; backend=$backend"
