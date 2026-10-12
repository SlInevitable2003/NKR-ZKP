# Groth16 vendored dependencies

`depends/` is a source snapshot copied from `/home/slinevit2003/lightZK/depends`
on 2026-10-12. It is intentionally committed directly into NKR-ZKP rather than
referenced as a Git submodule.

The snapshot includes the complete working trees needed by Groth16:

- libsnark and its libff, libfqfft, ate-pairing, gtest, xbyak and supercop dependencies;
- NVIDIA CGBN headers;
- local, previously uncommitted changes from the source checkout.

In particular, `libff/algebra/fields/bigint.tcc` uses a per-thread
`std::mt19937_64` generator. This avoids sharing a random generator between
OpenMP workers and avoids constructing `std::random_device` for every limb.

All nested `.git` directories, Git indirection files and `.gitmodules` files
were removed during import. Updates must be copied as source and reviewed in
the main NKR-ZKP Git history.
