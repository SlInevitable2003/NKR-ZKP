#include "groth16/backend.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"
#include "libfqfft/evaluation_domain/get_evaluation_domain.hpp"

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
    #pragma omp parallel for
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
        const auto lagrange = domain->evaluate_all_lagrange_polynomials(t);

        std::vector<Scalar> at(columns, Scalar::zero());
        std::vector<Scalar> bt(columns, Scalar::zero());
        std::vector<Scalar> ct(columns, Scalar::zero());
        const auto accumulate = [&](const CsrMatrix<Scalar> &matrix, std::vector<Scalar> &result) {
            for (std::size_t row = 0; row < rows; ++row)
                for (std::size_t j = matrix.row_ptr[row]; j < matrix.row_ptr[row + 1]; ++j)
                    result[matrix.col_idx[j]] += matrix.values[j] * lagrange[row];
        };
        accumulate(circuit.a, at); accumulate(circuit.b, bt); accumulate(circuit.c, ct);

        pk.a_query.resize(columns); pk.b_g1_query.resize(columns); pk.b_g2_query.resize(columns);
        pk.l_query.resize(columns, G1::zero()); pk.h_query.resize(rows, G1::zero());
        vk.ic.resize(circuit.public_inputs + 1);
        const G1 g1 = G1::one(); const G2 g2 = G2::one();
        #pragma omp parallel for
        for (std::size_t i = 0; i < columns; ++i) {
            pk.a_query[i] = at[i] * g1;
            pk.b_g1_query[i] = bt[i] * g1;
            pk.b_g2_query[i] = bt[i] * g2;
            if (i > circuit.public_inputs)
                pk.l_query[i] = ((beta * at[i] + alpha * bt[i] + ct[i]) * delta_inverse) * g1;
        }
        #pragma omp parallel for
        for (std::size_t i = 0; i <= circuit.public_inputs; ++i)
            vk.ic[i] = ((beta * at[i] + alpha * bt[i] + ct[i]) * gamma_inverse) * g1;

        Scalar power = Scalar::one();
        for (std::size_t i = 0; i + 1 < rows; ++i) {
            pk.h_query[i] = (power * zt * delta_inverse) * g1;
            power *= t;
        }

        pk.alpha_g1 = alpha * g1; pk.beta_g1 = beta * g1; pk.beta_g2 = beta * g2;
        pk.delta_g1 = delta * g1; pk.delta_g2 = delta * g2;
        vk.alpha_g1_beta_g2 = Curve::reduced_pairing(pk.alpha_g1, pk.beta_g2);
        vk.gamma_g2 = gamma * g2; vk.delta_g2 = pk.delta_g2;
    }

    void prove(const Circuit &circuit, const ProvingKey &pk,
               const std::vector<Scalar> &witness, RawProof &proof) override
    {
        const std::size_t rows = circuit.constraints();
        auto domain = libfqfft::get_evaluation_domain<Scalar>(rows);
        if (!domain || domain->m != rows) throw std::invalid_argument("unsupported R1CS domain size");

        auto az = multiply(circuit.a, witness);
        auto bz = multiply(circuit.b, witness);
        auto cz = multiply(circuit.c, witness);
        domain->iFFT(az); domain->iFFT(bz); domain->iFFT(cz);
        const Scalar coset = Scalar::multiplicative_generator;
        domain->cosetFFT(az, coset); domain->cosetFFT(bz, coset); domain->cosetFFT(cz, coset);
        #pragma omp parallel for
        for (std::size_t i = 0; i < rows; ++i) az[i] = az[i] * bz[i] - cz[i];
        domain->divide_by_Z_on_coset(az);
        domain->icosetFFT(az, coset);

        proof.ar = multiexp(pk.a_query, witness);
        proof.bs1 = multiexp(pk.b_g1_query, witness);
        proof.bs2 = multiexp(pk.b_g2_query, witness);
        proof.zk = multiexp(pk.l_query, witness);
        proof.qz = multiexp(pk.h_query, az);
    }
};

} // namespace

std::unique_ptr<Backend> make_cpu_backend() { return std::make_unique<CpuBackend>(); }

} // namespace nkr::groth16

