# NKR-ZKP

NKR-ZKP is an experimental multi-protocol, multi-backend zero-knowledge proof
framework. The current tree separates protocol semantics from reusable compute
operators:

```text
operators/
  cuda/                    shared BN254 CUDA field/curve/NTT/MSM primitives
  hip/                     shared BN254 HIP field/curve/NTT/MSM primitives

protocols/
  groth16/
    app/                   gen/setup/prove/verify CLI
    common/                protocol types, I/O, verification, backend registry
    backends/{cpu,cuda,hip}
  plonk/
    app/                   placeholder gen/setup/prove/verify CLI
    common/                public types/API and backend registry scaffold
    backends/{cpu,cuda,hip}
  stark/winterfell/

third_party/lightzk/       vendored libsnark/libff/libfqfft/CGBN snapshot
docs/                      architecture, provenance, and protocol documents
```

The CUDA and HIP operator sources are shared by Groth16 and PLONK. Protocol
specific work remains in each protocol backend: Groth16 owns R1CS/QAP/SpMV,
while PLONK will own permutation, quotient, linearisation, and opening logic.
CUDA and HIP implement the same conceptual operator boundary but retain
separate kernels so each platform can be tuned and audited independently.

## Current status

- Groth16 has working CPU, CUDA, and HIP backends. CPU is always built; CUDA
  or HIP can be selected at configure time.
- PLONK is a C++ interface and directory scaffold only. Its targets compile,
  but the apps deliberately exit with status 2 because protocol semantics are
  not implemented yet.
- The old Rust PLONK CPU/CUDA snapshots have been removed. The intended new
  implementation is the BN254, KZG-based protocol described in
  `docs/plonk_protocol_low_level.pdf`.
- Winterfell remains a separate Rust CPU implementation.

## Build and test

Run commands from the repository root:

```bash
make help
make check

# Groth16 CPU end-to-end flow
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4

# NVIDIA CUDA; use the compute capability of the installed GPU
make groth16-test-cuda CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=12

# HIP; use the offload target reported by the installed toolchain
make groth16-test-hip HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=12

# Build only the PLONK scaffold targets (no proof is generated yet)
make plonk-scaffold

# Winterfell example
make stark-matmul THREADS=4 MATRIX_SIZE=4
```

`groth16-dcu` and `groth16-test-dcu` remain compatibility aliases for the
canonical `groth16-hip` targets. Runtime backend selection uses
`--backend cpu|cuda|hip`; `dcu` is accepted as a compatibility alias.

The CMake accelerator option is shared by all protocols:

```text
-DNKR_ZKP_ACCELERATOR=NONE|CUDA|HIP
```

Only one accelerator toolchain is enabled in a build tree. CPU support remains
available in all configurations. Delete or reconfigure the shared CMake cache
before switching between CUDA and HIP.

## Groth16 manual flow

The four programs are emitted to `build/groth16/bin` by default:

```bash
make groth16-cpu
G16=build/groth16/bin
"$G16/groth16-gen" matmul 4
"$G16/groth16-setup" --backend cpu matmul_4.bin
"$G16/groth16-prove" --backend cpu matmul_4.bin
"$G16/groth16-verify" matmul_4.bin
```

Add `--profile` to print protocol and backend stage timings. Generated circuit,
witness, key, and proof artifacts are stored under `build/groth16/data` unless
the data directory is overridden.

## Layering rules

- `app/` parses commands and performs file orchestration only.
- `protocols/*/common` owns public protocol types, transcript/I/O rules,
  verification, and backend-neutral semantics.
- `protocols/*/backends` owns protocol-specific accelerated data flow and
  fused kernels.
- `operators/*` owns protocol-independent field, curve, NTT, MSM, memory, and
  future scan/batch-inversion primitives.
- Public protocol headers must not expose `cudaStream_t`, `hipStream_t`, or
  device pointers.
- A protocol must not include another protocol's backend headers.

See [docs/architecture.md](docs/architecture.md) for the detailed dependency
direction and migration constraints, and [docs/provenance.md](docs/provenance.md)
for imported source provenance.

## Common variables

- `JOBS`: build parallelism, default `nproc`;
- `PROFILE=1`: enable supported profiling output;
- `CUDA_ARCH`: NVIDIA compute capability;
- `HIP_ARCH`: HIP offload target;
- `LIGHTZK_DEPS_DIR`: vendored dependency root, default
  `third_party/lightzk/depends`;
- `GROTH16_BUILD_DIR`: CMake build directory, default `build/groth16`;
- `GROTH16_BIN_DIR`: executable directory, default `build/groth16/bin`;
- `GROTH16_DATA_DIR`: Groth16 artifact directory, default
  `build/groth16/data`.
