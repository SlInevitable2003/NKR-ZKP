#include "groth16/backend.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"
#include "libfqfft/evaluation_domain/get_evaluation_domain.hpp"
#include "nkr/operators/fixed_base_msm.hpp"
#include "profile.hpp"

namespace nkr::groth16 {
namespace {

template <typename Group>
Group multiexp(const std::vector<Group> &bases, const std::vector<Scalar> &scalars)
{
    if (bases.size() != scalars.size()) throw std::invalid_argument("MSM input size mismatch");
    if (bases.empty()) return Group::zero();
    return libff::multi_exp<Group, Scalar, libff::multi_exp_method::multi_exp_method_BDLO12>(
        bases.begin(), bases.end(), scalars.begin(), scalars.end(), 1);
}

std::vector<Scalar> multiply(const CsrMatrix<Scalar> &matrix, const std::vector<Scalar> &vector)
{
    std::vector<Scalar> result(matrix.num_rows, Scalar::zero());
    for (std::size_t row = 0; row < matrix.num_rows; ++row) {
        Scalar value = Scalar::zero();
        for (std::size_t j = matrix.row_ptr[row]; j < matrix.row_ptr[row + 1]; ++j)
            value += matrix.values[j] * vector[matrix.col_idx[j]];
        result[row] = value;
    }
    return result;
}

class CpuBackend final : public Backend {
public:
    const char *name() const override { return "cpu"; }

    void setup(const Circuit &circuit, ProvingKey &pk, VerificationKey &vk) override
    {
        ProfileScope total("groth16.cpu.setup.total");
        const std::size_t rows = circuit.constraints();
        const std::size_t columns = circuit.a.num_cols;
        auto domain = libfqfft::get_evaluation_domain<Scalar>(rows);
        if (!domain || domain->m != rows) throw std::invalid_argument("unsupported R1CS domain size");

        const Scalar t = Scalar::random_element();
        const Scalar alpha = Scalar::random_element();
        const Scalar beta = Scalar::random_element();
        const Scalar gamma = Scalar::random_element();
        const Scalar delta = Scalar::random_element();
        const Scalar gamma_inverse = gamma.inverse();
        const Scalar delta_inverse = delta.inverse();
        const Scalar zt = domain->compute_vanishing_polynomial(t);
        std::vector<Scalar> lagrange;
        {
            ProfileScope stage("groth16.cpu.setup.evaluate_lagrange");
            lagrange = domain->evaluate_all_lagrange_polynomials(t);
        }

        std::vector<Scalar> at(columns, Scalar::zero());
        std::vector<Scalar> bt(columns, Scalar::zero());
        std::vector<Scalar> ct(columns, Scalar::zero());
        const auto accumulate = [&](const CsrMatrix<Scalar> &matrix, std::vector<Scalar> &result) {
            for (std::size_t row = 0; row < rows; ++row)
                for (std::size_t j = matrix.row_ptr[row]; j < matrix.row_ptr[row + 1]; ++j)
                    result[matrix.col_idx[j]] += matrix.values[j] * lagrange[row];
        };
        {
            ProfileScope stage("groth16.cpu.setup.evaluate_r1cs");
            #pragma omp parallel sections
            {
                #pragma omp section
                accumulate(circuit.a, at);
                #pragma omp section
                accumulate(circuit.b, bt);
                #pragma omp section
                accumulate(circuit.c, ct);
            }
        }

        const G1 g1 = G1::one(); const G2 g2 = G2::one();

        std::vector<Scalar> l_scalars(columns, Scalar::zero());
        std::vector<Scalar> ic_scalars(circuit.public_inputs + 1);
        std::vector<Scalar> h_scalars(rows, Scalar::zero());
        {
            ProfileScope stage("groth16.cpu.setup.prepare_query_scalars");
            #pragma omp parallel for
            for (std::size_t i = 0; i < columns; ++i) {
                if (i > circuit.public_inputs)
                    l_scalars[i] =
                        (beta * at[i] + alpha * bt[i] + ct[i]) * delta_inverse;
            }
            #pragma omp parallel for
            for (std::size_t i = 0; i <= circuit.public_inputs; ++i)
                ic_scalars[i] =
                    (beta * at[i] + alpha * bt[i] + ct[i]) * gamma_inverse;

            Scalar power = Scalar::one();
            for (std::size_t i = 0; i + 1 < rows; ++i) {
                h_scalars[i] = power * zt * delta_inverse;
                power *= t;
            }
        }

        using G1FixedBaseMsm = operators::FixedBaseMsm<G1, Scalar>;
        using G2FixedBaseMsm = operators::FixedBaseMsm<G2, Scalar>;
        std::unique_ptr<G1FixedBaseMsm> g1_fixed_base_msm;
        std::unique_ptr<G2FixedBaseMsm> g2_fixed_base_msm;
        {
            ProfileScope stage("groth16.cpu.setup.fixed_base_tables");
            const std::size_t g1_scalar_count =
                3 * columns + rows + circuit.public_inputs + 1;
            g1_fixed_base_msm =
                std::make_unique<G1FixedBaseMsm>(g1, g1_scalar_count);
            g2_fixed_base_msm =
                std::make_unique<G2FixedBaseMsm>(g2, columns);
        }
        {
            ProfileScope stage("groth16.cpu.setup.generate_a_query");
            pk.a_query = (*g1_fixed_base_msm)(at);
        }
        {
            ProfileScope stage("groth16.cpu.setup.generate_b_g1_query");
            pk.b_g1_query = (*g1_fixed_base_msm)(bt);
        }
        {
            ProfileScope stage("groth16.cpu.setup.generate_b_g2_query");
            pk.b_g2_query = (*g2_fixed_base_msm)(bt);
        }
        {
            ProfileScope stage("groth16.cpu.setup.generate_l_query");
            pk.l_query = (*g1_fixed_base_msm)(l_scalars);
        }
        {
            ProfileScope stage("groth16.cpu.setup.generate_h_query");
            pk.h_query = (*g1_fixed_base_msm)(h_scalars);
        }
        {
            ProfileScope stage("groth16.cpu.setup.generate_ic_query");
            vk.ic = (*g1_fixed_base_msm)(ic_scalars);
        }

        {
            ProfileScope stage("groth16.cpu.setup.finalize_verification_key");
            pk.alpha_g1 = alpha * g1; pk.beta_g1 = beta * g1; pk.beta_g2 = beta * g2;
            pk.delta_g1 = delta * g1; pk.delta_g2 = delta * g2;
            vk.alpha_g1_beta_g2 = Curve::reduced_pairing(pk.alpha_g1, pk.beta_g2);
            vk.gamma_g2 = gamma * g2; vk.delta_g2 = pk.delta_g2;
        }
    }

    void prove(const Circuit &circuit, const ProvingKey &pk,
               const std::vector<Scalar> &witness, RawProof &proof) override
    {
        ProfileScope total("groth16.cpu.prove.total");
        const std::size_t rows = circuit.constraints();
        auto domain = libfqfft::get_evaluation_domain<Scalar>(rows);
        if (!domain || domain->m != rows) throw std::invalid_argument("unsupported R1CS domain size");

        std::vector<Scalar> az, bz, cz;
        {
            ProfileScope stage("groth16.cpu.prove.r1cs_matrix_vector");
            az = multiply(circuit.a, witness);
            bz = multiply(circuit.b, witness);
            cz = multiply(circuit.c, witness);
        }
        {
            ProfileScope stage("groth16.cpu.prove.quotient_polynomial");
            domain->iFFT(az); domain->iFFT(bz); domain->iFFT(cz);
            const Scalar coset = Scalar::multiplicative_generator;
            domain->cosetFFT(az, coset); domain->cosetFFT(bz, coset); domain->cosetFFT(cz, coset);
            for (std::size_t i = 0; i < rows; ++i) az[i] = az[i] * bz[i] - cz[i];
            domain->divide_by_Z_on_coset(az);
            domain->icosetFFT(az, coset);
        }

        {
            ProfileScope stage("groth16.cpu.prove.msm_a");
            proof.ar = multiexp(pk.a_query, witness);
        }
        {
            ProfileScope stage("groth16.cpu.prove.msm_b_g1");
            proof.bs1 = multiexp(pk.b_g1_query, witness);
        }
        {
            ProfileScope stage("groth16.cpu.prove.msm_b_g2");
            proof.bs2 = multiexp(pk.b_g2_query, witness);
        }
        {
            ProfileScope stage("groth16.cpu.prove.msm_l");
            proof.zk = multiexp(pk.l_query, witness);
        }
        {
            ProfileScope stage("groth16.cpu.prove.msm_h");
            proof.qz = multiexp(pk.h_query, az);
        }
    }
};

} // namespace

std::unique_ptr<Backend> make_cpu_backend() { return std::make_unique<CpuBackend>(); }

} // namespace nkr::groth16
