#include "groth16/backend.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include "api.h"
#include "profile.hpp"
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
    HIP_CHECK(hipDeviceSynchronize());
    HIP_CHECK(hipGetLastError());
}

class HipSetupLayout {
public:
    g1_t::affine_t *g1_base = nullptr;
    g2_t::affine_t *g2_base = nullptr;
    QAPContext<fr_t, Scalar> qap;
    uint32_t *column_offsets[3]{}, *row_indices[3]{};
    fr_t *matrix_values[3]{};
    fr_t *trapdoors = nullptr;
    fr_t *t_powers = nullptr;
    fr_t *at = nullptr, *bt = nullptr, *ct = nullptr;
    fr_t *lt = nullptr, *ht = nullptr, *ict = nullptr;
    g1_t *a_query = nullptr, *b_g1_query = nullptr, *l_query = nullptr;
    g1_t *h_query = nullptr, *ic = nullptr;
    g2_t *b_g2_query = nullptr;
    TypedGpuArena arena;

    HipSetupLayout(size_t variables, size_t constraints,
                   const CsrMatrix<Scalar> *matrices[3], Scalar omega)
        : qap(constraints, omega)
    {
        const size_t columns = variables + 1;
        arena.register_alloc(g1_base, 1);
        arena.register_alloc(g2_base, 1);
        arena.register_alloc(trapdoors, 6);
        arena.register_alloc(t_powers, constraints);
        arena.register_alloc(at, columns);
        arena.register_alloc(bt, columns);
        arena.register_alloc(ct, columns);
        arena.register_alloc(lt, columns);
        arena.register_alloc(ict, columns);
        arena.register_alloc(ht, constraints);
        arena.register_alloc(a_query, columns);
        arena.register_alloc(b_g1_query, columns);
        arena.register_alloc(b_g2_query, columns);
        arena.register_alloc(l_query, columns);
        arena.register_alloc(h_query, constraints);
        arena.register_alloc(ic, columns);
        for (size_t i = 0; i < 3; ++i) {
            arena.register_alloc(column_offsets[i], columns + 1);
            arena.register_alloc(row_indices[i], matrices[i]->col_idx.size());
            arena.register_alloc(matrix_values[i], matrices[i]->values.size());
        }
        arena.commit("Groth16 HIP setup");
    }
};

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
    std::unique_ptr<HipProveLayout> prepared_layout;

public:
    const char *name() const override { return "hip"; }

    void setup(const Circuit &circuit, ProvingKey &pk, VerificationKey &vk) override
    {
        ProfileScope total("groth16.hip.setup.total");
        const size_t rows = circuit.constraints();
        const size_t columns = circuit.a.num_cols;
        const size_t variables = columns - 1;
        const size_t exponent = libff::log2(rows);
        const Scalar omega =
            reinterpret_cast<const Scalar *>(forward_roots_of_unity)[exponent];
        const CsrMatrix<Scalar> *matrices[3] = {
            &circuit.a, &circuit.b, &circuit.c};
        HipSetupLayout layout(variables, rows, matrices, omega);

        const Scalar t = Scalar::random_element();
        const Scalar alpha = Scalar::random_element();
        const Scalar beta = Scalar::random_element();
        const Scalar gamma = Scalar::random_element();
        const Scalar delta = Scalar::random_element();
        const Scalar delta_inverse = delta.inverse();
        const Scalar gamma_inverse = gamma.inverse();
        const Scalar z_t = (t ^ rows) - Scalar::one();
        {
            ProfileScope stage("groth16.hip.setup.upload_inputs");
            G1 g1 = G1::one();
            G2 g2 = G2::one();
            g1.to_affine_coordinates();
            g2.to_affine_coordinates();
            static_assert(sizeof(G1) == sizeof(g1_t));
            static_assert(sizeof(G2) == sizeof(g2_t));
            HIP_CHECK(hipMemcpy(layout.g1_base, &g1, sizeof(g1_t::affine_t),
                                hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(layout.g2_base, &g2, sizeof(g2_t::affine_t),
                                hipMemcpyHostToDevice));
            const Scalar trapdoors[6] = {
                t, alpha, beta, delta_inverse, z_t, gamma_inverse};
            HIP_CHECK(hipMemcpy(layout.trapdoors, trapdoors, sizeof(trapdoors),
                                hipMemcpyHostToDevice));
            layout.qap.prepare(t, z_t);
            for (size_t i = 0; i < 3; ++i) {
                const auto csc = CscMatrix<Scalar>::from_csr(*matrices[i]);
                HIP_CHECK(hipMemcpy(layout.column_offsets[i], csc.col_ptr.data(),
                                    csc.col_ptr.size() * sizeof(uint32_t),
                                    hipMemcpyHostToDevice));
                HIP_CHECK(hipMemcpy(layout.row_indices[i], csc.row_idx.data(),
                                    csc.row_idx.size() * sizeof(uint32_t),
                                    hipMemcpyHostToDevice));
                HIP_CHECK(hipMemcpy(layout.matrix_values[i], csc.values.data(),
                                    csc.values.size() * sizeof(fr_t),
                                    hipMemcpyHostToDevice));
            }
        }

        const size_t public_inputs = circuit.public_inputs;
        {
            ProfileScope stage("groth16.hip.setup.generate_queries");
            HIP_CHECK(hipMemset(layout.t_powers, 0, rows * sizeof(fr_t)));
            HIP_CHECK(hipMemcpy(layout.t_powers + 1, &t, sizeof(fr_t),
                                hipMemcpyHostToDevice));
            qap_power_table_kernel<<<1, 1>>>(layout.t_powers, rows);
            layout.qap.compute_lagrange();

            constexpr size_t block = 256;
            fr_t *evaluations[3] = {layout.at, layout.bt, layout.ct};
            for (size_t i = 0; i < 3; ++i)
                transpose_spmv_kernel<<<ceil_div(columns, block), block>>>(
                    layout.column_offsets[i], layout.row_indices[i],
                    layout.matrix_values[i], layout.qap.lagrange(),
                    evaluations[i], columns);

            kernel<<<ceil_div(columns, block), block>>>(
                [] __device__(const fr_t *at, const fr_t *bt, const fr_t *ct,
                              const fr_t *trapdoors, fr_t *lt, size_t count,
                              size_t public_count) {
                    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                    if (i >= count) return;
                    if (i <= public_count) { lt[i].zero(); return; }
                    lt[i] = (trapdoors[2] * at[i] + trapdoors[1] * bt[i] +
                             ct[i]) * trapdoors[3];
                }, layout.at, layout.bt, layout.ct, layout.trapdoors,
                layout.lt, columns, public_inputs);
            kernel<<<ceil_div(public_inputs + 1, block), block>>>(
                [] __device__(const fr_t *at, const fr_t *bt, const fr_t *ct,
                              const fr_t *trapdoors, fr_t *ic, size_t count) {
                    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                    if (i < count)
                        ic[i] = (trapdoors[2] * at[i] + trapdoors[1] * bt[i] +
                                 ct[i]) * trapdoors[5];
                }, layout.at, layout.bt, layout.ct, layout.trapdoors,
                layout.ict, public_inputs + 1);
            kernel<<<ceil_div(rows, block), block>>>(
                [] __device__(const fr_t *powers, const fr_t *trapdoors,
                              fr_t *ht, size_t count) {
                    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                    if (i >= count) return;
                    if (i + 1 == count) { ht[i].zero(); return; }
                    ht[i] = powers[i] * trapdoors[4] * trapdoors[3];
                }, layout.t_powers, layout.trapdoors, layout.ht, rows);

            fixmsm_kernel<fr_t, g1_t::affine_t, g1_t>
                <<<ceil_div(columns, block), block>>>(
                    layout.g1_base, layout.at, layout.a_query, columns);
            fixmsm_kernel<fr_t, g1_t::affine_t, g1_t>
                <<<ceil_div(columns, block), block>>>(
                    layout.g1_base, layout.bt, layout.b_g1_query, columns);
            fixmsm_kernel<fr_t, g2_t::affine_t, g2_t>
                <<<ceil_div(columns, block), block>>>(
                    layout.g2_base, layout.bt, layout.b_g2_query, columns);
            fixmsm_kernel<fr_t, g1_t::affine_t, g1_t>
                <<<ceil_div(columns, block), block>>>(
                    layout.g1_base, layout.lt, layout.l_query, columns);
            fixmsm_kernel<fr_t, g1_t::affine_t, g1_t>
                <<<ceil_div(rows, block), block>>>(
                    layout.g1_base, layout.ht, layout.h_query, rows);
            fixmsm_kernel<fr_t, g1_t::affine_t, g1_t>
                <<<ceil_div(public_inputs + 1, block), block>>>(
                    layout.g1_base, layout.ict, layout.ic, public_inputs + 1);
            HIP_CHECK(hipDeviceSynchronize());
            HIP_CHECK(hipGetLastError());
        }

        {
            ProfileScope stage("groth16.hip.setup.download_queries");
            pk.a_query.resize(columns);
            pk.b_g1_query.resize(columns);
            pk.b_g2_query.resize(columns);
            pk.l_query.resize(columns);
            pk.h_query.resize(rows);
            vk.ic.resize(public_inputs + 1);
            HIP_CHECK(hipMemcpy(pk.a_query.data(), layout.a_query,
                                columns * sizeof(g1_t), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(pk.b_g1_query.data(), layout.b_g1_query,
                                columns * sizeof(g1_t), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(pk.b_g2_query.data(), layout.b_g2_query,
                                columns * sizeof(g2_t), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(pk.l_query.data(), layout.l_query,
                                columns * sizeof(g1_t), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(pk.h_query.data(), layout.h_query,
                                rows * sizeof(g1_t), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(vk.ic.data(), layout.ic,
                                (public_inputs + 1) * sizeof(g1_t),
                                hipMemcpyDeviceToHost));
            for (auto &point : pk.a_query) point.to_affine_coordinates();
            for (auto &point : pk.b_g1_query) point.to_affine_coordinates();
            for (auto &point : pk.b_g2_query) point.to_affine_coordinates();
            for (auto &point : pk.l_query) point.to_affine_coordinates();
            for (auto &point : pk.h_query) point.to_affine_coordinates();
            for (auto &point : vk.ic) point.to_affine_coordinates();
        }

        pk.alpha_g1 = alpha * G1::one();
        pk.beta_g1 = beta * G1::one();
        pk.beta_g2 = beta * G2::one();
        pk.delta_g1 = delta * G1::one();
        pk.delta_g2 = delta * G2::one();
        vk.alpha_g1_beta_g2 = Curve::reduced_pairing(pk.alpha_g1, pk.beta_g2);
        vk.gamma_g2 = gamma * G2::one();
        vk.delta_g2 = pk.delta_g2;
    }

    void prepare(const Circuit &circuit, const ProvingKey &pk) override
    {
        ProfileScope total("groth16.hip.prepare.total");
        const std::size_t rows = circuit.constraints();
        const std::size_t variables = circuit.variables();
        CsrMatrix<Scalar> *matrices[3] = {
            const_cast<CsrMatrix<Scalar> *>(&circuit.a),
            const_cast<CsrMatrix<Scalar> *>(&circuit.b),
            const_cast<CsrMatrix<Scalar> *>(&circuit.c)};
        {
            ProfileScope stage("groth16.hip.prepare.initialize_layout");
            prepared_layout = std::make_unique<HipProveLayout>(
                variables, rows, matrices,
                reinterpret_cast<const Scalar *>(forward_roots_of_unity)[
                    libff::log2(rows)]);
            profile_synchronize();
        }
        {
            ProfileScope stage("groth16.hip.prepare.upload_key");
            prepared_layout->sparse_g1.load_bases(pk.a_query.data(), true, 0);
            prepared_layout->sparse_g1.load_bases(pk.b_g1_query.data(), true, 1);
            prepared_layout->sparse_g1.load_bases(pk.l_query.data(), true, 2);
            prepared_layout->dense_g1.load_bases(pk.h_query.data());
            prepared_layout->sparse_g2.load_bases(pk.b_g2_query.data());
            profile_synchronize();
        }
    }

    void prove(const Circuit &circuit, const ProvingKey &pk,
               const std::vector<Scalar> &witness, RawProof &proof) override
    {
        if (!prepared_layout) prepare(circuit, pk);
        ProfileScope total("groth16.hip.prove.total");
        HipProveLayout &layout = *prepared_layout;
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
