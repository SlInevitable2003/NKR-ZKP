# Groth16 vendored dependencies

`depends/` is a source snapshot copied from `/home/slinevit2003/lightZK/depends`
on 2026-10-12. It is intentionally committed directly into NKR-ZKP rather than
referenced as a Git submodule.

The snapshot includes the source trees needed by NKR-ZKP's Groth16 implementation:

- libsnark and the required libff/libfqfft sources;
- NVIDIA CGBN headers;
- local, previously uncommitted changes from the source checkout.

In particular, `libff/algebra/fields/bigint.tcc` uses a per-thread
`std::mt19937_64` generator. This avoids sharing a random generator between
OpenMP workers and avoids constructing `std::random_device` for every limb.

All nested `.git` directories, Git indirection files and `.gitmodules` files
were removed during import. Updates must be copied as source and reviewed in
the main NKR-ZKP Git history.

The snapshot is intentionally trimmed for the framework's fixed ALT_BN128
configuration. Upstream GoogleTest, demos, profiles, tutorials, test vectors,
ADSNARK/SUPERCOP code, and the legacy BN128 ate-pairing/xbyak backend are not
vendored. NKR-ZKP's protocol-level integration tests are the maintained test
surface.
