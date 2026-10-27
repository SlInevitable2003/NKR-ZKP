#include "groth16/backend.hpp"

#include <memory>

#include "api.h"
#include "groth16/profile.hpp"
#include "libff/common/utils.hpp"

namespace nkr::groth16 {
namespace {

using namespace alt_bn128;

unsigned choose_window_bits(std::size_t terms)
{
    if (terms <= 4096) return 8;
    if (terms <= 65536) return 11;
    return 13;
}

void profile_synchronize()
{
    if (!profiling_enabled()) return;
    CUDA_CHECK(hipDeviceSynchronize());
    CUDA_CHECK(hipGetLastError());
}

class HipProveLayout {
public:
    BucketContext<fr_t, Scalar> witness_buckets, quotient_buckets;
    spMVMContext<fr_t, Scalar, 3> spmvm;
    NTTContext<fr_t, Scalar> ntt;
    MSMContext<fr_t, g1_t::affine_t, g1_t, g1_bucket_t, Scalar, G1, false, 3> sparse_g1;
    MSMContext<fr_t, g1_t::affine_t, g1_t, g1_bucket_t, Scalar, G1> dense_g1;
    MSMContext<fr_t, g2_t::affine_t, g2_t, g2_bucket_t, Scalar, G2, true> sparse_g2;
    fr_t *polynomials[3];
    TypedGpuArena arena;

    HipProveLayout(std::size_t variables, std::size_t constraints,
                   CsrMatrix<Scalar> *matrices[3], Scalar omega)
        : witness_buckets(variables + 1, choose_window_bits(variables + 1)),
          quotient_buckets(constraints, choose_window_bits(constraints)),
          spmvm(constraints, variables + 1, matrices),
          ntt(constraints, omega, Scalar::multiplicative_generator),
          sparse_g1(variables + 1, choose_window_bits(variables + 1)),
          dense_g1(constraints, choose_window_bits(constraints)),
          sparse_g2(variables + 1, choose_window_bits(variables + 1))
    {
        arena.register_alloc(polynomials[0], constraints);
        arena.register_alloc(polynomials[1], constraints);
        arena.register_alloc(polynomials[2], constraints);
        arena.commit("Groth16 HIP polynomial buffer");
    }
};

class HipBackend final : public Backend {
public:
    const char *name() const override { return "hip"; }

    void setup(const Circuit &circuit, ProvingKey &pk, VerificationKey &vk) override
    {
        ProfileScope total("groth16.hip.setup.total");
        // The imported HIP implementation accelerates proving only. Setup has
        // identical protocol semantics and deliberately uses the CPU backend.
        make_cpu_backend()->setup(circuit, pk, vk);
    }

    void prove(const Circuit &circuit, const ProvingKey &pk,
               const std::vector<Scalar> &witness, RawProof &proof) override
    {
        ProfileScope total("groth16.hip.prove.total");
        const std::size_t rows = circuit.constraints();
        const std::size_t variables = circuit.variables();
        CsrMatrix<Scalar> *matrices[3] = {
            const_cast<CsrMatrix<Scalar> *>(&circuit.a),
            const_cast<CsrMatrix<Scalar> *>(&circuit.b),
            const_cast<CsrMatrix<Scalar> *>(&circuit.c)};
        HipProveLayout layout(variables, rows, matrices,
            reinterpret_cast<const Scalar *>(forward_roots_of_unity)[libff::log2(rows)]);
        {
            ProfileScope stage("groth16.hip.prove.upload_key");
            layout.sparse_g1.load_bases(pk.a_query.data(), true, 0);
            layout.sparse_g1.load_bases(pk.b_g1_query.data(), true, 1);
            layout.sparse_g1.load_bases(pk.l_query.data(), true, 2);
            layout.dense_g1.load_bases(pk.h_query.data());
            layout.sparse_g2.load_bases(pk.b_g2_query.data());
            profile_synchronize();
        }
        {
            ProfileScope stage("groth16.hip.prove.r1cs_matrix_vector");
            layout.witness_buckets.load_scalars(witness.data());
            layout.spmvm.spmvm(layout.witness_buckets.scalars, layout.polynomials);
            profile_synchronize();
        }
        {
            ProfileScope stage("groth16.hip.prove.quotient_polynomial");
            layout.ntt.intt(layout.polynomials[0]); layout.ntt.intt(layout.polynomials[1]);
            layout.ntt.intt(layout.polynomials[2]); layout.ntt.coset_ntt(layout.polynomials[0]);
            layout.ntt.coset_ntt(layout.polynomials[1]); layout.ntt.coset_ntt(layout.polynomials[2]);
            layout.ntt.A_times_B_minus_C_divided_by_Z(layout.polynomials[0], layout.polynomials[1], layout.polynomials[2]);
            layout.ntt.coset_intt(layout.polynomials[0]);
            layout.quotient_buckets.load_scalars(layout.polynomials[0]);
            profile_synchronize();
        }
        {
            ProfileScope stage("groth16.hip.prove.witness_msms");
            layout.witness_buckets.process();
            layout.sparse_g1.msm(layout.witness_buckets, &proof.ar, 0);
            layout.sparse_g1.msm(layout.witness_buckets, &proof.bs1, 1);
            layout.sparse_g1.msm(layout.witness_buckets, &proof.zk, 2);
            layout.sparse_g2.msm(layout.witness_buckets, &proof.bs2);
            profile_synchronize();
        }
        {
            ProfileScope stage("groth16.hip.prove.quotient_msm");
            layout.quotient_buckets.process();
            layout.dense_g1.msm(layout.quotient_buckets, &proof.qz);
            profile_synchronize();
        }
    }
};

} // namespace

std::unique_ptr<Backend> make_hip_backend() { return std::make_unique<HipBackend>(); }

} // namespace nkr::groth16
