#include "plonk/arithmetization.hpp"
#include "plonk/backend.hpp"
#include "plonk/protocol.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "libfqfft/evaluation_domain/get_evaluation_domain.hpp"

using namespace nkr::plonk;

namespace {

UniversalSrs make_test_srs(const std::size_t max_degree, const Scalar &tau)
{
    // Test-only trusted setup: knowing tau makes this SRS unsafe for production.
    UniversalSrs srs;
    srs.g1_powers.resize(max_degree + 1);
    Scalar power = Scalar::one();
    for (G1 &point : srs.g1_powers) {
        point = power * G1::one();
        power *= tau;
    }
    srs.g2 = G2::one();
    srs.tau_g2 = tau * srs.g2;
    return srs;
}

Scalar evaluate(const std::vector<Scalar> &coefficients, const Scalar &point)
{
    Scalar result = Scalar::zero();
    for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
        result *= point;
        result += *it;
    }
    return result;
}

void check_polynomial(const PolynomialData &polynomial,
                      const G1 &commitment,
                      const DomainParameters &parameters,
                      const Scalar &tau)
{
    const std::size_t n = parameters.domain_size;
    assert(polynomial.evaluations.size() == n);
    assert(polynomial.coefficients.size() == n);
    assert(polynomial.coset_evaluations.size() == parameters.quotient_domain_size);

    const auto domain = libfqfft::get_evaluation_domain<Scalar>(n);
    std::vector<Scalar> reconstructed = polynomial.coefficients;
    domain->FFT(reconstructed);
    assert(reconstructed == polynomial.evaluations);
    assert(commitment == evaluate(polynomial.coefficients, tau) * G1::one());

    const auto quotient_domain =
        libfqfft::get_evaluation_domain<Scalar>(parameters.quotient_domain_size);
    reconstructed = polynomial.coefficients;
    reconstructed.resize(parameters.quotient_domain_size, Scalar::zero());
    quotient_domain->cosetFFT(reconstructed, parameters.coset_shift);
    assert(reconstructed == polynomial.coset_evaluations);
}

std::vector<Scalar> expected_sigma(const std::vector<std::uint32_t> &permutation,
                                   const DomainParameters &parameters)
{
    const std::size_t n = parameters.domain_size;
    const std::array<Scalar, 3> cosets = {
        Scalar::one(), parameters.k_1, parameters.k_2};
    std::vector<Scalar> roots(n);
    roots[0] = Scalar::one();
    for (std::size_t row = 1; row < n; ++row) {
        roots[row] = roots[row - 1] * parameters.omega;
    }

    std::vector<Scalar> result(n);
    for (std::size_t row = 0; row < n; ++row) {
        const std::size_t destination = permutation[row];
        result[row] = cosets[destination / n] * roots[destination % n];
    }
    return result;
}

bool setup_rejects(Backend &backend,
                   const Circuit &circuit,
                   const UniversalSrs &srs)
{
    ProvingKey proving_key;
    VerificationKey verification_key;
    try {
        setup(backend, circuit, srs, proving_key, verification_key);
    } catch (const std::invalid_argument &) {
        return true;
    }
    return false;
}

} // namespace

int main()
{
    Curve::init_public_params();

    PlonkishManager manager;
    const Variable x = manager.public_variable(Scalar(3));
    const Variable y = manager.public_variable(Scalar(4));
    const Variable sum = manager.add(x, y);
    const Variable product = manager.multiply(sum, y);
    const Variable expected = manager.constant(Scalar(28));
    manager.assert_equal(product, expected);
    const Arithmetization arithmetization = manager.finalize();

    const std::size_t n = arithmetization.circuit.domain_size;
    const Scalar tau(13);
    const UniversalSrs srs = make_test_srs(n + 2, tau);
    std::unique_ptr<Backend> backend = make_cpu_backend();

    ProvingKey proving_key;
    VerificationKey verification_key;
    setup(*backend, arithmetization.circuit, srs, proving_key, verification_key);

    const DomainParameters &domain = proving_key.domain;
    assert(domain.domain_size == n);
    assert(domain.quotient_domain_size == 4 * n);
    assert((domain.omega ^ n) == Scalar::one());
    assert(n == 1 || (domain.omega ^ (n / 2)) != Scalar::one());
    assert((domain.k_1 ^ n) != Scalar::one());
    assert((domain.k_2 ^ n) != Scalar::one());
    assert(((domain.k_2 * domain.k_1.inverse()) ^ n) != Scalar::one());
    assert((domain.coset_shift ^ domain.quotient_domain_size) != Scalar::one());

    assert(proving_key.fixed.q_l.evaluations == arithmetization.circuit.q_l);
    assert(proving_key.fixed.q_r.evaluations == arithmetization.circuit.q_r);
    assert(proving_key.fixed.q_m.evaluations == arithmetization.circuit.q_m);
    assert(proving_key.fixed.q_o.evaluations == arithmetization.circuit.q_o);
    assert(proving_key.fixed.q_c.evaluations == arithmetization.circuit.q_c);
    assert(proving_key.fixed.sigma_1.evaluations ==
           expected_sigma(arithmetization.circuit.sigma_1, domain));
    assert(proving_key.fixed.sigma_2.evaluations ==
           expected_sigma(arithmetization.circuit.sigma_2, domain));
    assert(proving_key.fixed.sigma_3.evaluations ==
           expected_sigma(arithmetization.circuit.sigma_3, domain));

    check_polynomial(proving_key.fixed.q_l, proving_key.commitments.q_l, domain, tau);
    check_polynomial(proving_key.fixed.q_r, proving_key.commitments.q_r, domain, tau);
    check_polynomial(proving_key.fixed.q_m, proving_key.commitments.q_m, domain, tau);
    check_polynomial(proving_key.fixed.q_o, proving_key.commitments.q_o, domain, tau);
    check_polynomial(proving_key.fixed.q_c, proving_key.commitments.q_c, domain, tau);
    check_polynomial(
        proving_key.fixed.sigma_1, proving_key.commitments.sigma_1, domain, tau);
    check_polynomial(
        proving_key.fixed.sigma_2, proving_key.commitments.sigma_2, domain, tau);
    check_polynomial(
        proving_key.fixed.sigma_3, proving_key.commitments.sigma_3, domain, tau);

    assert(verification_key.domain.domain_size == domain.domain_size);
    assert(verification_key.domain.omega == domain.omega);
    assert(verification_key.commitments.q_l == proving_key.commitments.q_l);
    assert(verification_key.commitments.sigma_3 == proving_key.commitments.sigma_3);
    assert(verification_key.g2 == srs.g2);
    assert(verification_key.tau_g2 == srs.tau_g2);

    UniversalSrs short_srs = srs;
    short_srs.g1_powers.resize(n + 2);
    assert(setup_rejects(*backend, arithmetization.circuit, short_srs));

    UniversalSrs invalid_srs = srs;
    invalid_srs.g1_powers[1] = G1::zero();
    assert(setup_rejects(*backend, arithmetization.circuit, invalid_srs));

    Circuit invalid_permutation = arithmetization.circuit;
    invalid_permutation.sigma_1[0] = invalid_permutation.sigma_1[1];
    assert(setup_rejects(*backend, invalid_permutation, srs));
    return 0;
}
