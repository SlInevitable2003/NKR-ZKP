# Groth16 CUDA adapter

This directory contains Groth16-specific CUDA orchestration, QAP, and sparse
matrix operations. Shared BN254 field, curve, NTT, MSM, and runtime code lives
in `operators/cuda` and is linked through `nkr_operators_cuda`.

Build and run from the repository root with `make groth16-test-cuda`.
