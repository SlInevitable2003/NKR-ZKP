# Shared operators

This directory contains protocol-independent compute primitives shared by
Groth16 and PLONK. Protocol equations, proof types, transcripts, QAP logic,
and PLONK permutation/quotient logic do not belong here.

- `cuda/`: NVIDIA implementations of the BN254 field, curve, NTT, MSM, and
  device-runtime helpers.
- `hip/`: HIP implementations of the same operator family.

CUDA and HIP deliberately keep separate kernels. They implement the same
conceptual operator boundary and are tested by protocol-independent vectors,
but they are not forced through a macro-based common source file.

CPU protocol backends currently use the vendored libff/libfqfft primitives
directly. A CPU operator facade can be added when more than one protocol has a
working CPU implementation that benefits from it.
