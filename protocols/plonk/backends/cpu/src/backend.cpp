#include "plonk/backend.hpp"
#include "plonk/protocol.hpp"

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"
#include "libfqfft/evaluation_domain/get_evaluation_domain.hpp"

namespace nkr::plonk {
namespace {

bool is_power_of_two(const std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

std::size_t next_power_of_two(const std::size_t value)
{
    if (value == 0) {
        return 1;
    }
    if (value > (std::numeric_limits<std::size_t>::max() >> 1) + 1) {
        throw std::invalid_argument("PLONK Setup: quotient domain size overflows size_t");
    }

    std::size_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

void require_size(const std::size_t actual,
                  const std::size_t expected,
                  const char *name)
{
    if (actual != expected) {
        throw std::invalid_argument(
            "PLONK Setup: " + std::string(name) + " must contain exactly " +
            std::to_string(expected) + " entries");
    }
}

void validate_circuit(const Circuit &circuit)
{
    const std::size_t n = circuit.domain_size;
    if (!is_power_of_two(n)) {
        throw std::invalid_argument("PLONK Setup: domain size must be a non-zero power of two");
    }
    if (circuit.rows > n) {
        throw std::invalid_argument("PLONK Setup: circuit row count exceeds the domain size");
    }
    if (circuit.public_inputs > circuit.rows) {
        throw std::invalid_argument("PLONK Setup: public input count exceeds the circuit row count");
    }

    require_size(circuit.q_l.size(), n, "q_l");
    require_size(circuit.q_r.size(), n, "q_r");
    require_size(circuit.q_m.size(), n, "q_m");
    require_size(circuit.q_o.size(), n, "q_o");
    require_size(circuit.q_c.size(), n, "q_c");
    require_size(circuit.sigma_1.size(), n, "sigma_1");
    require_size(circuit.sigma_2.size(), n, "sigma_2");
    require_size(circuit.sigma_3.size(), n, "sigma_3");

    if (n > std::numeric_limits<std::uint32_t>::max() / 3) {
        throw std::invalid_argument("PLONK Setup: encoded permutation does not fit in uint32_t");
    }

    std::vector<bool> seen(3 * n, false);
    const std::array<const std::vector<std::uint32_t> *, 3> sigma = {
        &circuit.sigma_1, &circuit.sigma_2, &circuit.sigma_3};
    for (const auto *column : sigma) {
        for (const std::uint32_t destination : *column) {
            if (destination >= 3 * n) {
                throw std::invalid_argument("PLONK Setup: permutation cell is outside the circuit");
            }
            if (seen[destination]) {
                throw std::invalid_argument("PLONK Setup: wiring map is not a permutation");
            }
            seen[destination] = true;
        }
    }
}

void validate_srs(const UniversalSrs &srs, const std::size_t n)
{
    if (srs.g1_powers.size() < n + 3) {
        throw std::invalid_argument(
            "PLONK Setup: SRS must contain powers [tau^0] through [tau^(N+2)]");
    }
    for (std::size_t i = 0; i < n + 3; ++i) {
        if (srs.g1_powers[i].is_zero() || !srs.g1_powers[i].is_well_formed()) {
            throw std::invalid_argument("PLONK Setup: SRS contains an invalid G1 power");
        }
    }
    if (srs.g2.is_zero() || srs.tau_g2.is_zero() ||
        !srs.g2.is_well_formed() || !srs.tau_g2.is_well_formed()) {
        throw std::invalid_argument("PLONK Setup: SRS contains an invalid G2 point");
    }
}

std::pair<Scalar, Scalar> choose_wire_cosets(const std::size_t n)
{
    for (std::uint64_t first_value = 2; first_value < 1024; ++first_value) {
        const Scalar first(first_value);
        if ((first ^ n) == Scalar::one()) {
            continue;
        }
        const Scalar first_inverse = first.inverse();
        for (std::uint64_t second_value = 2; second_value < 1024; ++second_value) {
            const Scalar second(second_value);
            if ((second ^ n) == Scalar::one()) {
                continue;
            }
            if (((second * first_inverse) ^ n) != Scalar::one()) {
                return {first, second};
            }
        }
    }
    throw std::runtime_error("PLONK Setup: failed to choose disjoint wire cosets");
}

Scalar choose_quotient_coset(const std::size_t quotient_domain_size)
{
    Scalar candidate = Scalar::multiplicative_generator;
    if ((candidate ^ quotient_domain_size) != Scalar::one()) {
        return candidate;
    }
    for (std::uint64_t value = 2; value < 1024; ++value) {
        candidate = Scalar(value);
        if ((candidate ^ quotient_domain_size) != Scalar::one()) {
            return candidate;
        }
    }
    throw std::runtime_error("PLONK Setup: failed to choose an extended-domain coset");
}

G1 commit_polynomial(const std::vector<Scalar> &coefficients,
                     const UniversalSrs &srs)
{
    return libff::multi_exp<G1, Scalar, libff::multi_exp_method_BDLO12>(
        srs.g1_powers.begin(),
        srs.g1_powers.begin() + coefficients.size(),
        coefficients.begin(),
        coefficients.end(),
        1);
}

PolynomialData prepare_polynomial(
    const std::vector<Scalar> &evaluations,
    const std::shared_ptr<libfqfft::evaluation_domain<Scalar>> &domain,
    const std::shared_ptr<libfqfft::evaluation_domain<Scalar>> &quotient_domain,
    const Scalar &coset_shift,
    const UniversalSrs &srs,
    G1 &commitment)
{
    PolynomialData result;
    result.evaluations = evaluations;
    result.coefficients = evaluations;
    domain->iFFT(result.coefficients);
    commitment = commit_polynomial(result.coefficients, srs);

    result.coset_evaluations = result.coefficients;
    result.coset_evaluations.resize(quotient_domain->m, Scalar::zero());
    quotient_domain->cosetFFT(result.coset_evaluations, coset_shift);
    return result;
}

std::vector<Scalar> make_sigma_evaluations(
    const std::vector<std::uint32_t> &permutation,
    const std::vector<Scalar> &roots,
    const Scalar &k_1,
    const Scalar &k_2)
{
    const std::size_t n = roots.size();
    const std::array<Scalar, 3> column_cosets = {Scalar::one(), k_1, k_2};
    std::vector<Scalar> evaluations(n);
    for (std::size_t row = 0; row < n; ++row) {
        const std::size_t destination = permutation[row];
        const std::size_t destination_column = destination / n;
        const std::size_t destination_row = destination % n;
        evaluations[row] = column_cosets[destination_column] * roots[destination_row];
    }
    return evaluations;
}

class CpuBackend final : public Backend {
public:
    const char *name() const override { return "cpu"; }

    void setup(const Circuit &circuit,
               const UniversalSrs &srs,
               ProvingKey &proving_key,
               VerificationKey &verification_key) override
    {
        validate_circuit(circuit);
        const std::size_t n = circuit.domain_size;
        validate_srs(srs, n);
        if (n > (std::numeric_limits<std::size_t>::max() - 6) / 3) {
            throw std::invalid_argument("PLONK Setup: quotient domain size overflows size_t");
        }

        const auto domain = libfqfft::get_evaluation_domain<Scalar>(n);
        if (!domain || domain->m != n) {
            throw std::invalid_argument("PLONK Setup: the scalar field does not support this domain");
        }

        const std::size_t quotient_size = next_power_of_two(3 * n + 6);
        const auto quotient_domain = libfqfft::get_evaluation_domain<Scalar>(quotient_size);
        if (!quotient_domain || quotient_domain->m != quotient_size) {
            throw std::invalid_argument(
                "PLONK Setup: the scalar field does not support the quotient domain");
        }

        const Scalar omega = domain->get_domain_element(1);
        const auto [k_1, k_2] = choose_wire_cosets(n);
        const Scalar coset_shift = choose_quotient_coset(quotient_size);

        std::vector<Scalar> roots(n);
        roots[0] = Scalar::one();
        for (std::size_t row = 1; row < n; ++row) {
            roots[row] = roots[row - 1] * omega;
        }

        const std::vector<Scalar> sigma_1 =
            make_sigma_evaluations(circuit.sigma_1, roots, k_1, k_2);
        const std::vector<Scalar> sigma_2 =
            make_sigma_evaluations(circuit.sigma_2, roots, k_1, k_2);
        const std::vector<Scalar> sigma_3 =
            make_sigma_evaluations(circuit.sigma_3, roots, k_1, k_2);

        ProvingKey result;
        result.circuit = circuit;
        result.srs = srs;
        result.domain = {n, quotient_size, omega, k_1, k_2, coset_shift};

        result.fixed.q_l = prepare_polynomial(
            circuit.q_l, domain, quotient_domain, coset_shift, srs, result.commitments.q_l);
        result.fixed.q_r = prepare_polynomial(
            circuit.q_r, domain, quotient_domain, coset_shift, srs, result.commitments.q_r);
        result.fixed.q_m = prepare_polynomial(
            circuit.q_m, domain, quotient_domain, coset_shift, srs, result.commitments.q_m);
        result.fixed.q_o = prepare_polynomial(
            circuit.q_o, domain, quotient_domain, coset_shift, srs, result.commitments.q_o);
        result.fixed.q_c = prepare_polynomial(
            circuit.q_c, domain, quotient_domain, coset_shift, srs, result.commitments.q_c);
        result.fixed.sigma_1 = prepare_polynomial(
            sigma_1, domain, quotient_domain, coset_shift, srs, result.commitments.sigma_1);
        result.fixed.sigma_2 = prepare_polynomial(
            sigma_2, domain, quotient_domain, coset_shift, srs, result.commitments.sigma_2);
        result.fixed.sigma_3 = prepare_polynomial(
            sigma_3, domain, quotient_domain, coset_shift, srs, result.commitments.sigma_3);

        proving_key = std::move(result);
        verification_key.domain = proving_key.domain;
        verification_key.commitments = proving_key.commitments;
        verification_key.g2 = srs.g2;
        verification_key.tau_g2 = srs.tau_g2;
    }

    void prove(const ProvingKey &,
               const Witness &,
               Proof &) override
    {
        throw_scaffold_only();
    }
};

} // namespace

std::unique_ptr<Backend> make_cpu_backend()
{
    return std::make_unique<CpuBackend>();
}

} // namespace nkr::plonk
