#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "libff/common/profiling.hpp"
#include <libff/common/utils.hpp>

#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"

#include <omp.h>
using namespace std;

#include "api.h"
using namespace alt_bn128;
typedef libsnark::default_r1cs_ppzksnark_pp ppT;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

template <typename ppT>
struct Groth16RawProof {
    libff::G1<ppT> Ar, Bs1, zK, qZ;
    libff::G2<ppT> Bs2;
};

template <typename ppT>
struct Groth16Proof {
    libff::G1<ppT> g_A;
    libff::G2<ppT> g_B;
    libff::G1<ppT> g_C;
};

struct Groth16ProveGPULayout {
    BucketContext<fr_t, libff::Fr<ppT>> z_bucket_ctx, mz_bucket_ctx;
    spMVMContext<fr_t, libff::Fr<ppT>, 3> spmvm_ctx;
    NTTContext<fr_t, libff::Fr<ppT>> ntt_ctx;
    MSMContext<fr_t, g1_t::affine_t, g1_t, g1_bucket_t, libff::Fr<ppT>, libff::G1<ppT>> g1_sparse_msm_ctx_0, g1_sparse_msm_ctx_1, g1_sparse_msm_ctx_2;
    MSMContext<fr_t, g1_t::affine_t, g1_t, g1_bucket_t, libff::Fr<ppT>, libff::G1<ppT>> g1_dense_msm_ctx;
    MSMContext<fr_t, g2_t::affine_t, g2_t, g2_bucket_t, libff::Fr<ppT>, libff::G2<ppT>> g2_sparse_msm_ctx;

    fr_t *polys[3];
    TypedGpuArena arena;

    Groth16ProveGPULayout(
        size_t n, size_t m, CsrMatrix<libff::Fr<ppT>> **mats, libff::Fr<ppT> omega, libff::Fr<ppT> coset, size_t window_bits = 13) :
        z_bucket_ctx(1 + n, window_bits), mz_bucket_ctx(m, window_bits),
        spmvm_ctx(m, 1 + n, mats),
        ntt_ctx(m, omega, coset),
        g1_sparse_msm_ctx_0(1 + n, window_bits), g1_sparse_msm_ctx_1(1 + n, window_bits), g1_sparse_msm_ctx_2(1 + n, window_bits),
        g1_dense_msm_ctx(m, window_bits),
        g2_sparse_msm_ctx(1 + n, window_bits)
    {
        arena.register_alloc(polys[0], m);
        arena.register_alloc(polys[1], m);
        arena.register_alloc(polys[2], m);
        arena.commit("PolyBuffer");
    }
};

static void cuda_prove_setup(Groth16ProveGPULayout &gpu_layout, const vector<libff::G1<ppT>> &pkA1, const vector<libff::G1<ppT>> &pkB1, const vector<libff::G1<ppT>> &pkK, const vector<libff::G1<ppT>> &pkZ, const vector<libff::G2<ppT>> &pkB2)
{
    gpu_layout.g1_sparse_msm_ctx_0.load_bases(pkA1.data());
    gpu_layout.g1_sparse_msm_ctx_1.load_bases(pkB1.data());
    gpu_layout.g1_sparse_msm_ctx_2.load_bases(pkK.data()); // 在 0..=k 处填充为 [0]_1
    gpu_layout.g1_dense_msm_ctx.load_bases(pkZ.data()); // 在 m-1 处填充为 [0]_1
    gpu_layout.g2_sparse_msm_ctx.load_bases(pkB2.data());
}

static void cuda_prove_compute(Groth16ProveGPULayout &gpu_layout,
                               const vector<libff::Fr<ppT>> &z,
                               Groth16RawProof<ppT> &result)
{
    gpu_layout.z_bucket_ctx.load_scalars(z.data());
    gpu_layout.spmvm_ctx.spmvm(gpu_layout.z_bucket_ctx.scalars, gpu_layout.polys);

    gpu_layout.ntt_ctx.intt(gpu_layout.polys[0]);
    gpu_layout.ntt_ctx.intt(gpu_layout.polys[1]);
    gpu_layout.ntt_ctx.intt(gpu_layout.polys[2]);
    gpu_layout.ntt_ctx.coset_ntt(gpu_layout.polys[0]);
    gpu_layout.ntt_ctx.coset_ntt(gpu_layout.polys[1]);
    gpu_layout.ntt_ctx.coset_ntt(gpu_layout.polys[2]);
    gpu_layout.ntt_ctx.A_times_B_minus_C_divided_by_Z(gpu_layout.polys[0], gpu_layout.polys[1], gpu_layout.polys[2]);
    gpu_layout.ntt_ctx.coset_intt(gpu_layout.polys[0]);
    gpu_layout.mz_bucket_ctx.load_scalars(gpu_layout.polys[0]);

    gpu_layout.z_bucket_ctx.process();
    gpu_layout.g1_sparse_msm_ctx_0.msm(gpu_layout.z_bucket_ctx, &result.Ar);
    gpu_layout.g1_sparse_msm_ctx_1.msm(gpu_layout.z_bucket_ctx, &result.Bs1);
    gpu_layout.g1_sparse_msm_ctx_2.msm(gpu_layout.z_bucket_ctx, &result.zK);
    gpu_layout.g2_sparse_msm_ctx.msm(gpu_layout.z_bucket_ctx, &result.Bs2);

    gpu_layout.mz_bucket_ctx.process();
    gpu_layout.g1_dense_msm_ctx.msm(gpu_layout.mz_bucket_ctx, &result.qZ);
}

template <typename ppT>
static Groth16Proof<ppT> blind_proof(const Groth16RawProof<ppT> &raw, const libff::G1<ppT> &alpha_g1, const libff::G1<ppT> &beta_g1, const libff::G1<ppT> &delta_g1, const libff::G2<ppT> &beta_g2, const libff::G2<ppT> &delta_g2)
{
    const libff::Fr<ppT> r = libff::Fr<ppT>::random_element();
    const libff::Fr<ppT> s = libff::Fr<ppT>::random_element();

    const libff::G1<ppT> g1_A = alpha_g1 + raw.Ar + r * delta_g1;
    const libff::G1<ppT> g1_B = beta_g1 + raw.Bs1 + s * delta_g1;

    Groth16Proof<ppT> proof;
    proof.g_A = g1_A;
    proof.g_B = beta_g2 + raw.Bs2 + s * delta_g2;
    proof.g_C = raw.qZ + raw.zK + s * g1_A + r * g1_B - (r * s) * delta_g1;
    return proof;
}

static bool check_witness(const CsrMatrix<libff::Fr<ppT>> &A,
                          const CsrMatrix<libff::Fr<ppT>> &B,
                          const CsrMatrix<libff::Fr<ppT>> &C,
                          const vector<libff::Fr<ppT>> &z)
{
    const size_t m = A.num_rows;
    bool ok = true;
    for (size_t r = 0; r < m; r++) {
        libff::Fr<ppT> az = libff::Fr<ppT>::zero(), bz = libff::Fr<ppT>::zero(), cz = libff::Fr<ppT>::zero();
        for (size_t j = A.row_ptr[r]; j < A.row_ptr[r + 1]; j++) az += A.values[j] * z[A.col_idx[j]];
        for (size_t j = B.row_ptr[r]; j < B.row_ptr[r + 1]; j++) bz += B.values[j] * z[B.col_idx[j]];
        for (size_t j = C.row_ptr[r]; j < C.row_ptr[r + 1]; j++) cz += C.values[j] * z[C.col_idx[j]];
        if (az * bz != cz) {
            if (ok) cout << "Witness check FAILED at constraints:\n";
            cout << "  row " << r << "\n";
            ok = false;
        }
    }
    if (ok) cout << "Witness satisfies all R1CS constraints." << endl;
    return ok;
}

static void usage(const char *prog) {
    cerr << "Usage: " << prog << " [--path <dir>] <r1cs-file>\n"
         << "  Reads the R1CS and witness written by gen.cu (<stem>.bin,\n"
         << "  <stem>.witness.bin) and the proving key written by setup.cu\n"
         << "  (<stem>.pk.bin), computes a randomized (zero-knowledge) Groth16\n"
         << "  proof (A, B, C) on the GPU and writes it to <stem>.proof.bin.\n";
    exit(1);
}

int main(int argc, char *argv[]) {
    ppT::init_public_params();

    string data_dir = APP_DATA_DIR;
    string filename;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--path") { if (i + 1 >= argc) usage(argv[0]); data_dir = argv[++i]; } 
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); } 
        else if (!arg.empty() && arg[0] == '-') { cerr << "Unknown option: " << arg << "\n"; usage(argv[0]); }
        else { filename = arg; }
    }
    if (filename.empty()) usage(argv[0]);

    string stem = filename;
    const string ext = ".bin";
    if (stem.size() >= ext.size() && stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0) stem.erase(stem.size() - ext.size());

    const string r1cs_path    = data_dir + "/" + filename;
    const string pk_path      = data_dir + "/" + stem + ".pk.bin";
    const string witness_path = data_dir + "/" + stem + ".witness.bin";
    const string proof_path   = data_dir + "/" + stem + ".proof.bin";

    libff::enter_block("Reading R1CS from file");
    BinaryArchive ar;
    ar.open_for_read(r1cs_path);

    size_t k;
    ar.read(k);

    CsrMatrix<libff::Fr<ppT>> A, B, C;
    size_t num_cols;
    ar.read(num_cols);

    ar.read(A.row_ptr); ar.read(A.col_idx); ar.read(A.values);
    ar.read(B.row_ptr); ar.read(B.col_idx); ar.read(B.values);
    ar.read(C.row_ptr); ar.read(C.col_idx); ar.read(C.values);
    ar.close();
    libff::leave_block("Reading R1CS from file");

    A.num_rows = A.row_ptr.size() - 1;  A.num_cols = num_cols;
    B.num_rows = B.row_ptr.size() - 1;  B.num_cols = num_cols;
    C.num_rows = C.row_ptr.size() - 1;  C.num_cols = num_cols;

    const size_t m = A.num_rows;
    const size_t n = num_cols - 1;

    cout << "Loaded R1CS from " << r1cs_path << endl;
    cout << "* public inputs : " << k << "\n"
         << "* variables     : " << n << " (private: " << (n - k) << ")\n"
         << "* constraints   : " << m << "\n";

    libff::enter_block("Reading proving key from file");
    BinaryArchive par;
    par.open_for_read(pk_path);

    vector<libff::G1<ppT>> pkA1, pkB1, pkK, pkZ;
    vector<libff::G2<ppT>> pkB2;
    libff::G1<ppT> alpha_g1, beta_g1, delta_g1;
    libff::G2<ppT> beta_g2, delta_g2;
    par.read(pkA1); par.read(pkB1); par.read(pkB2); par.read(pkK); par.read(pkZ);
    par.read(alpha_g1); par.read(beta_g1); par.read(beta_g2); par.read(delta_g1); par.read(delta_g2);
    par.close();
    libff::leave_block("Reading proving key from file");

    cout << "Loaded proving key from " << pk_path << endl;
    if (pkA1.size() != 1 + n || pkB1.size() != 1 + n || pkB2.size() != 1 + n || pkK.size() != 1 + n || pkZ.size() != m) {
        cerr << "Proving key size mismatch with R1CS "
                "(expected pkA1/pkB1/pkB2/pkK = 1+n and pkZ = m).\n";
        return 1;
    }

    libff::enter_block("Reading witness from file");
    BinaryArchive war;
    war.open_for_read(witness_path);
    
    vector<libff::Fr<ppT>> z;
    war.read(z);
    war.close();
    libff::leave_block("Reading witness from file");
    if (z.size() != 1 + n) {
        cerr << "Witness size mismatch (expected 1+n = " << (1 + n)
             << ", got " << z.size() << ").\n";
        return 1;
    }

    libff::enter_block("Validating witness against R1CS");
    const bool witness_ok = check_witness(A, B, C, z);
    libff::leave_block("Validating witness against R1CS");
    if (!witness_ok) cerr << "Warning: witness does NOT satisfy the R1CS; proof will be invalid.\n";

    if (m < 2048) {
        cerr << "Number of constraints (" << m
             << ") is too small for the GPU NTT (needs >= 2048). "
                "Generate a larger circuit with gen.\n";
        return 1;
    }

    CsrMatrix<libff::Fr<ppT>> *mats[3] = { &A, &B, &C };
    const size_t exp = log2_floor(m);
    Groth16ProveGPULayout gpu_layout(n, m, mats, reinterpret_cast<const libff::Fr<ppT>*>(forward_roots_of_unity)[exp], libff::Fr<ppT>::multiplicative_generator);

    libff::enter_block("GPU Groth16 Prove Setup");
    cuda_prove_setup(gpu_layout, pkA1, pkB1, pkK, pkZ, pkB2);
    libff::leave_block("GPU Groth16 Prove Setup");

    Groth16RawProof<ppT> raw;
    libff::enter_block("GPU Groth16 Prove Compute");
    cuda_prove_compute(gpu_layout, z, raw);
    libff::leave_block("GPU Groth16 Prove Compute");

    libff::enter_block("Randomize proof (zero-knowledge)");
    const Groth16Proof<ppT> proof = blind_proof<ppT>(raw, alpha_g1, beta_g1, delta_g1, beta_g2, delta_g2);
    libff::leave_block("Randomize proof (zero-knowledge)");

    libff::enter_block("Writing proof to file");
    BinaryArchive out;
    out.open_for_write(proof_path);
    out.write(proof.g_A);
    out.write(proof.g_B);
    out.write(proof.g_C);
    out.close();
    libff::leave_block("Writing proof to file");

    cout << "Wrote proof to " << proof_path << endl;
    return 0;
}
