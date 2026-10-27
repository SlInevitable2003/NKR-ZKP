# Groth16 HIP adapter

This directory contains the Groth16-specific HIP proving adapter and SpMV
implementation. Shared BN254 field, curve, NTT, MSM, and runtime code lives in
`operators/hip` and is linked through `nkr_operators_hip`.

The imported HIP backend accelerates proving; setup currently delegates to the
CPU reference backend. Build and run from the repository root with
`make groth16-test-hip HIP_ARCH=<gfx-arch>`.
