# Architecture and evolution constraints

## Dependency direction

The repository has four layers:

```text
app
  -> protocol common API
       -> protocol backend adapter
            -> shared operators
                 -> vendor runtime/toolchain
```

Dependencies only point downward. Shared operators never include Groth16 or
PLONK types, and one protocol never includes another protocol's backend.

## Shared operators

`operators/cuda` and `operators/hip` contain the imported LightZK BN254
field, curve, NTT, MSM, and device-runtime primitives. Groth16 and PLONK both
link these targets instead of keeping protocol-local copies.

CUDA and HIP share contracts and test vectors, not kernel source. Their launch
strategies, reductions, memory APIs, and tuning are sufficiently different
that macro-based source unification would make both harder to maintain.

The following operations belong in the shared layer:

- BN254 field and extension-field arithmetic;
- G1/G2 representations and group arithmetic;
- NTT/iNTT and coset variants;
- G1/G2 MSM;
- device allocation, streams, error handling, and profiling hooks;
- future generic scan, prefix-product, and batch-inversion primitives.

R1CS, QAP, permutation products, quotient equations, transcripts, and proof
containers do not belong in the operator layer.

## Protocol layout

Groth16 and PLONK intentionally use the same directory and naming style:

```text
protocols/<protocol>/
  app/
  common/include/<protocol>/
  common/src/
  backends/cpu/
  backends/cuda/
  backends/hip/
  tests/
```

`app/` contains thin CLI entry points. `common/` owns protocol types, public
API, validation, canonical encoding, verification, and backend selection.
Backend directories contain platform adapters and protocol-specific fused
kernels. Public common headers use host-side types only.

Groth16 is the working reference for external `gen/setup/prove/verify`
organization. Its current coarse `Backend::setup/prove` interface is not a
requirement for PLONK internals: PLONK may use round-oriented sessions to keep
polynomials resident on a GPU while the common layer owns transcript order.

## PLONK scope

The PLONK tree is currently a scaffold. The removed Rust snapshots implemented
a different BLS12-family, four-wire, lookup-enabled system and are not the new
implementation base. The planned first implementation is:

- BN254/ALT_BN128;
- KZG commitment;
- classic three-wire arithmetic gates;
- permutation grand product;
- CPU reference first, then CUDA and HIP adapters over shared operators.

The proof container remains opaque until the transcript, challenge derivation,
canonical encoding, and exact low-level protocol are frozen.

## Build model

CPU support is always present. `NKR_ZKP_ACCELERATOR` selects at most one of
`NONE`, `CUDA`, or `HIP` for a CMake build tree. This avoids mixing incompatible
compiler drivers and runtimes while keeping the runtime backend choice between
CPU and the accelerator compiled into that build.

The vendored LightZK dependency snapshot is at `third_party/lightzk/depends`.
libff's x86-64 assembly is enabled only on matching hosts; other CPU
architectures use the portable implementation.

## Migration and review rules

1. Preserve a working Groth16 CPU build after every operator extraction.
2. Validate CUDA/HIP operator moves with protocol-independent vectors and the
   existing Groth16 end-to-end flow.
3. Establish deterministic PLONK CPU vectors before accelerator work.
4. Keep semantic changes separate from mechanical CUDA-to-HIP changes.
5. Use canonical serialization for new PLONK artifacts; do not expose raw
   libff or device-memory layouts as a cross-platform file format.
6. Keep generated build trees, kernels, and benchmark output out of Git.
