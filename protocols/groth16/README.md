# Groth16

Groth16 follows the repository-wide protocol layout:

- `app/`: platform-independent `gen/setup/prove/verify` programs;
- `common/`: protocol types, I/O, validation, verification, profiling, and
  backend selection;
- `backends/cpu`: libff/libfqfft reference implementation;
- `backends/cuda` and `backends/hip`: protocol adapters containing QAP, SpMV,
  and other Groth16-specific data flow.

BN254 field, curve, NTT, MSM, and device-runtime code is shared through the
top-level `operators/cuda` and `operators/hip` targets. It is not duplicated in
this protocol directory.

From the repository root:

```bash
make groth16-test-cpu
make groth16-test-cuda CUDA_ARCH=80
make groth16-test-hip HIP_ARCH=<gfx-arch>
```

CPU is always available. CUDA or HIP can be compiled alongside it, and setup
or prove selects the active implementation with `--backend cpu|cuda|hip`.
Generation and verification link only the protocol-common layer.
