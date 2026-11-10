#!/usr/bin/env bash
#SBATCH --job-name=plonk-data-modes
#SBATCH --partition=debug
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=00:30:00

set -euo pipefail

cd "${SLURM_SUBMIT_DIR:?Submit this job from the repository root}"
conda_prefix=${CONDA_PREFIX:?Activate a Conda environment containing GMP before submission}
build_dir=${PLONK_MODES_BUILD_DIR:-/tmp/plonk-data-modes-${SLURM_JOB_ID}}
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK:-4}
accelerator=${PLONK_MODES_ACCELERATOR:-NONE}
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
    echo "Unsupported PLONK_MODES_ACCELERATOR: $accelerator" >&2
    exit 1
fi
echo "PLONK data mode test backend: $backend"

cmake -S . -B "$build_dir" \
    -DNKR_ZKP_PROTOCOL=PLONK -DNKR_ZKP_ACCELERATOR="$accelerator" "${accelerator_flags[@]}" \
    -DNKR_ZKP_PLONK_DATA_DIR="$build_dir/data" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -UNDEBUG" \
    -DGMP_INCLUDE_DIR="$conda_prefix/include" \
    -DGMP_LIBRARY="$conda_prefix/lib/libgmp.so" \
    -DGMP_LIBRARIES="$conda_prefix/lib/libgmp.so" \
    -DGMPXX_LIBRARIES="$conda_prefix/lib/libgmpxx.so"
cmake --build "$build_dir" --parallel "$OMP_NUM_THREADS" \
    --target plonk-gen plonk-setup plonk-prove plonk-verify

bin_dir="$build_dir/bin"
for mode in dense sparse hybrid; do
    "$bin_dir/plonk-gen" --profile --data-mode "$mode" matmul 79
    "$bin_dir/plonk-gen" --profile --data-mode "$mode" fibonacci 10

    for circuit in matmul fibonacci; do
        case "$circuit" in
            matmul)   n=79  ;;
            fibonacci) n=10 ;;
        esac

        stem="${circuit}_$n"
        if [[ "$mode" != dense ]]; then stem="${stem}_${mode}"; fi
        "$bin_dir/plonk-setup" --profile --backend "$backend" "${stem}.bin"
        "$bin_dir/plonk-prove" --profile --backend "$backend" "${stem}.bin"
        "$bin_dir/plonk-verify" --profile "${stem}.bin"
        echo "PASS: $circuit $mode $backend"
    done
done
echo "PASS: all PLONK input data mode checks; backend=$backend"
