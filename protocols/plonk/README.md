# PLONK

This directory is the C++ scaffold for a BN254, KZG-based PLONK
implementation. Its layering intentionally matches Groth16:

- `app/`: platform-independent `gen/setup/prove/verify` entry points;
- `common/`: protocol types, public API, backend selection, and eventually
  transcript, canonical I/O, KZG, and verification;
- `backends/cpu`: libff/libfqfft reference backend;
- `backends/cuda` and `backends/hip`: thin protocol adapters over the shared
  top-level operators.

The previous Rust CPU/CUDA snapshots have been removed. They implemented a
different four-wire, lookup-enabled protocol over a different software stack
and are not the implementation base for this scaffold.

The targets currently compile but deliberately return exit status 2 with a
"scaffold only" message. No PLONK proof semantics are implemented yet.
