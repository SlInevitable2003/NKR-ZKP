#include "plonk/backend.hpp"
#include "profile.hpp"
#include "plonk/protocol.hpp"
#include "plonk/transcript.hpp"

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"
#include "libff/algebra/fields/field_utils.hpp"
#include "libfqfft/evaluation_domain/get_evaluation_domain.hpp"

namespace nkr::plonk {
namespace {

std::pair<Scalar, Scalar> choose_wire_cosets(const std::size_t n)
{
    for (std::uint64_t first_value = 2; first_value < 1024; ++first_value) {
        const Scalar first(first_value);
        if ((first ^ n) == Scalar::one()) { continue; }
        const Scalar first_inverse = first.inverse();
        for (std::uint64_t second_value = 2; second_value < 1024; ++second_value) {
            const Scalar second(second_value);
            if ((second ^ n) == Scalar::one()) { continue; }
            if (((second * first_inverse) ^ n) != Scalar::one()) { return {first, second}; }
        }
    }
    throw std::runtime_error("PLONK Setup: failed to choose disjoint wire cosets");
}

G1 commit_polynomial(const std::vector<Scalar> &coefficients, const UniversalSrs &srs)
{
    return libff::multi_exp<G1, Scalar, libff::multi_exp_method_BDLO12>(srs.g1_powers.begin(), srs.g1_powers.begin() + coefficients.size(), coefficients.begin(), coefficients.end(), 1);
}

Scalar evaluate_polynomial(const std::vector<Scalar> &coefficients, const Scalar &point)
{
    Scalar result = Scalar::zero();
    for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) result = result * point + *it;
    return result;
}

void add_scaled(std::vector<Scalar> &destination, const std::vector<Scalar> &source, const Scalar &scale)
{
    if (destination.size() < source.size()) destination.resize(source.size(), Scalar::zero());
    for (std::size_t i = 0; i < source.size(); ++i) destination[i] += scale * source[i];
}

std::vector<Scalar> blind_wire_polynomial(std::vector<Scalar> coefficients,
                                          const Scalar &linear_blind,
                                          const Scalar &constant_blind)
{
    const std::size_t n = coefficients.size();
    coefficients.resize(n + 2, Scalar::zero());
    coefficients[0] -= constant_blind;
    coefficients[1] -= linear_blind;
    coefficients[n] += constant_blind;
    coefficients[n + 1] += linear_blind;
    return coefficients;
}

std::vector<Scalar> blind_permutation_polynomial(
    std::vector<Scalar> coefficients,
    const Scalar &quadratic_blind,
    const Scalar &linear_blind,
    const Scalar &constant_blind)
{
    const std::size_t n = coefficients.size();
    coefficients.resize(n + 3, Scalar::zero());
    coefficients[0] -= constant_blind;
    coefficients[1] -= linear_blind;
    coefficients[2] -= quadratic_blind;
    coefficients[n] += constant_blind;
    coefficients[n + 1] += linear_blind;
    coefficients[n + 2] += quadratic_blind;
    return coefficients;
}

std::vector<Scalar> coset_evaluations(const std::vector<Scalar> &coefficients, const std::shared_ptr<libfqfft::evaluation_domain<Scalar>> &domain, const Scalar &shift)
{
    std::vector<Scalar> result = coefficients;
    result.resize(domain->m, Scalar::zero());
    domain->cosetFFT(result, shift);
    return result;
}

std::vector<Scalar> synthetic_division(std::vector<Scalar> polynomial,
                                       const Scalar &point,
                                       const Scalar &claimed_value)
{
    if (polynomial.size() < 2) throw std::invalid_argument("PLONK Prove: cannot open a constant polynomial");
    polynomial[0] -= claimed_value;
    std::vector<Scalar> quotient(polynomial.size() - 1, Scalar::zero());
    quotient.back() = polynomial.back();
    for (std::size_t i = quotient.size() - 1; i > 0; --i) quotient[i - 1] = polynomial[i] + point * quotient[i];
    if (polynomial[0] + point * quotient[0] != Scalar::zero()) throw std::runtime_error("PLONK Prove: opening value does not match polynomial");
    return quotient;
}

VerificationKey verification_key_from(const ProvingKey &key)
{
    return {key.domain, key.commitments, key.srs.g2, key.srs.tau_g2};
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
    const std::vector<Scalar> &omegas,
    const Scalar &k_1,
    const Scalar &k_2)
{
    const std::size_t n = omegas.size();
    const std::array<Scalar, 3> column_cosets = {Scalar::one(), k_1, k_2};
    std::vector<Scalar> evaluations(n);
    for (std::size_t row = 0; row < n; ++row) {
        const std::size_t destination = permutation[row];
        const std::size_t destination_column = destination / n;
        const std::size_t destination_row = destination % n;
        evaluations[row] = column_cosets[destination_column] * omegas[destination_row];
    }
    return evaluations;
}

class CpuBackend final : public Backend {
public:
    const char *name() const override { return "cpu"; }

    void setup(const Circuit &circuit, const UniversalSrs &srs, ProvingKey &proving_key, VerificationKey &verification_key) override
    {
        const std::size_t n = circuit.domain_size;
        if (n < 8) { throw std::invalid_argument("PLONK Setup: domain size must be at least 8"); }
        if (n > std::numeric_limits<std::size_t>::max() / 4) { throw std::invalid_argument("PLONK Setup: quotient domain size overflows size_t"); }

        const auto domain = libfqfft::get_evaluation_domain<Scalar>(n);
        if (!domain || domain->m != n) { throw std::invalid_argument("PLONK Setup: the scalar field does not support this domain"); }

        const std::size_t quotient_size = 4 * n;
        const auto quotient_domain = libfqfft::get_evaluation_domain<Scalar>(quotient_size);
        if (!quotient_domain || quotient_domain->m != quotient_size) { throw std::invalid_argument("PLONK Setup: the scalar field does not support the quotient domain"); }

        const Scalar omega = domain->get_domain_element(1);
        const auto [k_1, k_2] = choose_wire_cosets(n);
        const Scalar coset_shift = Scalar::multiplicative_generator;
        if ((coset_shift ^ quotient_size) == Scalar::one()) { throw std::invalid_argument("PLONK Setup: quotient coset intersects the evaluation domain"); }

        std::vector<Scalar> omegas(n);
        omegas[0] = Scalar::one();
        for (std::size_t row = 1; row < n; ++row) omegas[row] = omegas[row - 1] * omega;

        const std::vector<Scalar> sigma_1 = make_sigma_evaluations(circuit.sigma_1, omegas, k_1, k_2);
        const std::vector<Scalar> sigma_2 = make_sigma_evaluations(circuit.sigma_2, omegas, k_1, k_2);
        const std::vector<Scalar> sigma_3 = make_sigma_evaluations(circuit.sigma_3, omegas, k_1, k_2);

        ProvingKey result;
        result.circuit = circuit;
        result.srs = srs;
        result.domain = { n, quotient_size, omega, k_1, k_2, coset_shift };
        result.omegas = std::move(omegas);

        result.fixed.q_l = prepare_polynomial(circuit.q_l, domain, quotient_domain, coset_shift, srs, result.commitments.q_l);
        result.fixed.q_r = prepare_polynomial(circuit.q_r, domain, quotient_domain, coset_shift, srs, result.commitments.q_r);
        result.fixed.q_m = prepare_polynomial(circuit.q_m, domain, quotient_domain, coset_shift, srs, result.commitments.q_m);
        result.fixed.q_o = prepare_polynomial(circuit.q_o, domain, quotient_domain, coset_shift, srs, result.commitments.q_o);
        result.fixed.q_c = prepare_polynomial(circuit.q_c, domain, quotient_domain, coset_shift, srs, result.commitments.q_c);
        result.fixed.sigma_1 = prepare_polynomial(sigma_1, domain, quotient_domain, coset_shift, srs, result.commitments.sigma_1);
        result.fixed.sigma_2 = prepare_polynomial(sigma_2, domain, quotient_domain, coset_shift, srs, result.commitments.sigma_2);
        result.fixed.sigma_3 = prepare_polynomial(sigma_3, domain, quotient_domain, coset_shift, srs, result.commitments.sigma_3);

        proving_key = std::move(result);
        verification_key.domain = proving_key.domain;
        verification_key.commitments = proving_key.commitments;
        verification_key.g2 = srs.g2;
        verification_key.tau_g2 = srs.tau_g2;
    }

    void prove(const ProvingKey &key, const Witness &witness, Proof &proof) override
    {
        const std::size_t n = key.domain.domain_size;
        const std::size_t quotient_size = key.domain.quotient_domain_size;

        const auto domain = libfqfft::get_evaluation_domain<Scalar>(n);
        const auto quotient_domain = libfqfft::get_evaluation_domain<Scalar>(quotient_size);
        Transcript transcript = initialize_transcript(verification_key_from(key), witness.public_input);

        // Round 1
        std::vector<Scalar> a_coefficients = witness.a;
        std::vector<Scalar> b_coefficients = witness.b;
        std::vector<Scalar> c_coefficients = witness.c;
        domain->iFFT(a_coefficients);
        domain->iFFT(b_coefficients);
        domain->iFFT(c_coefficients);
        a_coefficients = blind_wire_polynomial(std::move(a_coefficients), Scalar::random_element(), Scalar::random_element());
        b_coefficients = blind_wire_polynomial(std::move(b_coefficients), Scalar::random_element(), Scalar::random_element());
        c_coefficients = blind_wire_polynomial(std::move(c_coefficients), Scalar::random_element(), Scalar::random_element());

        proof.a_commitment = commit_polynomial(a_coefficients, key.srs);
        proof.b_commitment = commit_polynomial(b_coefficients, key.srs);
        proof.c_commitment = commit_polynomial(c_coefficients, key.srs);
        transcript.append(proof.a_commitment);
        transcript.append(proof.b_commitment);
        transcript.append(proof.c_commitment);
        const Scalar beta = transcript.challenge();
        const Scalar gamma = transcript.challenge();

        // Round 2
        std::vector<Scalar> numerator(n);
        std::vector<Scalar> denominator(n);
        for (std::size_t i = 0; i < n; ++i) {
            numerator[i] = (witness.a[i] + beta * key.omegas[i] + gamma) * (witness.b[i] + beta * key.domain.k_1 * key.omegas[i] + gamma) * (witness.c[i] + beta * key.domain.k_2 * key.omegas[i] + gamma);
            denominator[i] = (witness.a[i] + beta * key.fixed.sigma_1.evaluations[i] + gamma) * (witness.b[i] + beta * key.fixed.sigma_2.evaluations[i] + gamma) * (witness.c[i] + beta * key.fixed.sigma_3.evaluations[i] + gamma);
        }
        libff::batch_invert(denominator);
        std::vector<Scalar> z_evaluations(n, Scalar::zero());
        z_evaluations[0] = Scalar::one();
        Scalar accumulator = Scalar::one();
        for (std::size_t i = 0; i < n; ++i) {
            accumulator *= numerator[i] * denominator[i];
            if (i + 1 < n) z_evaluations[i + 1] = accumulator;
        }
        if (accumulator != Scalar::one()) throw std::invalid_argument("PLONK Prove: witness violates the copy permutation");

        std::vector<Scalar> z_coefficients = z_evaluations;
        domain->iFFT(z_coefficients);
        z_coefficients = blind_permutation_polynomial(std::move(z_coefficients), Scalar::random_element(), Scalar::random_element(), Scalar::random_element());
        proof.z_commitment = commit_polynomial(z_coefficients, key.srs);
        transcript.append(proof.z_commitment);
        const Scalar alpha = transcript.challenge();
        const Scalar alpha_squared = alpha.squared();

        // Round 3
        std::vector<Scalar> public_input_coefficients = witness.public_input;
        domain->iFFT(public_input_coefficients);
        const std::vector<Scalar> a_coset = coset_evaluations(a_coefficients, quotient_domain, key.domain.coset_shift);
        const std::vector<Scalar> b_coset = coset_evaluations(b_coefficients, quotient_domain, key.domain.coset_shift);
        const std::vector<Scalar> c_coset = coset_evaluations(c_coefficients, quotient_domain, key.domain.coset_shift);
        const std::vector<Scalar> z_coset = coset_evaluations(z_coefficients, quotient_domain, key.domain.coset_shift);
        const std::vector<Scalar> pi_coset = coset_evaluations(public_input_coefficients, quotient_domain, key.domain.coset_shift);

        std::vector<Scalar> coset_points(quotient_size);
        std::vector<Scalar> z_h_evaluations(quotient_size);
        std::vector<Scalar> quotient_denominators(2 * quotient_size);
        std::vector<Scalar> quotient_evaluations(quotient_size);
        Scalar point = key.domain.coset_shift;
        const Scalar quotient_omega = quotient_domain->get_domain_element(1);
        const std::size_t omega_shift = quotient_size / n;
        const Scalar n_inverse = Scalar(static_cast<long>(n)).inverse();
        for (std::size_t i = 0; i < quotient_size; ++i) {
            coset_points[i] = point;
            const Scalar point_to_n = point ^ n;
            z_h_evaluations[i] = point_to_n - Scalar::one();
            quotient_denominators[i] = z_h_evaluations[i];
            quotient_denominators[quotient_size + i] = point - Scalar::one();
            point *= quotient_omega;
        }
        libff::batch_invert(quotient_denominators);
        for (std::size_t i = 0; i < quotient_size; ++i) {
            point = coset_points[i];
            const Scalar l_1 = z_h_evaluations[i] * n_inverse * quotient_denominators[quotient_size + i];
            const Scalar gate = a_coset[i] * key.fixed.q_l.coset_evaluations[i]
                              + b_coset[i] * key.fixed.q_r.coset_evaluations[i]
                              + a_coset[i] * b_coset[i] * key.fixed.q_m.coset_evaluations[i]
                              + c_coset[i] * key.fixed.q_o.coset_evaluations[i]
                              + key.fixed.q_c.coset_evaluations[i]
                              + pi_coset[i];
            const Scalar identity = (a_coset[i] + beta * point + gamma) * (b_coset[i] + beta * key.domain.k_1 * point + gamma) * (c_coset[i] + beta * key.domain.k_2 * point + gamma);
            const Scalar sigma = (a_coset[i] + beta * key.fixed.sigma_1.coset_evaluations[i] + gamma) * (b_coset[i] + beta * key.fixed.sigma_2.coset_evaluations[i] + gamma) * (c_coset[i] + beta * key.fixed.sigma_3.coset_evaluations[i] + gamma);
            const Scalar permutation = z_coset[i] * identity - z_coset[(i + omega_shift) % quotient_size] * sigma;
            quotient_evaluations[i] = (gate + alpha * permutation + alpha_squared * (z_coset[i] - Scalar::one()) * l_1) * quotient_denominators[i];
        }
        quotient_domain->icosetFFT(quotient_evaluations, key.domain.coset_shift);

        const std::size_t part_size = n + 2;
        std::vector<Scalar> t_lo(quotient_evaluations.begin(), quotient_evaluations.begin() + part_size);
        std::vector<Scalar> t_mid(quotient_evaluations.begin() + part_size, quotient_evaluations.begin() + 2 * part_size);
        std::vector<Scalar> t_hi(quotient_evaluations.begin() + 2 * part_size, quotient_evaluations.begin() + 3 * part_size);
        const Scalar quotient_blind_1 = Scalar::random_element();
        const Scalar quotient_blind_2 = Scalar::random_element();
        t_lo.resize(part_size + 1, Scalar::zero());
        t_mid.resize(part_size + 1, Scalar::zero());
        t_lo[part_size] += quotient_blind_1;
        t_mid[0] -= quotient_blind_1;
        t_mid[part_size] += quotient_blind_2;
        t_hi[0] -= quotient_blind_2;

        proof.t_lo_commitment = commit_polynomial(t_lo, key.srs);
        proof.t_mid_commitment = commit_polynomial(t_mid, key.srs);
        proof.t_hi_commitment = commit_polynomial(t_hi, key.srs);
        transcript.append(proof.t_lo_commitment);
        transcript.append(proof.t_mid_commitment);
        transcript.append(proof.t_hi_commitment);
        const Scalar zeta = transcript.challenge();
        const Scalar zeta_omega = zeta * key.domain.omega;
        const Scalar zeta_to_n = zeta ^ n;
        const Scalar z_h_zeta = zeta_to_n - Scalar::one();
        if (z_h_zeta.is_zero()) throw std::runtime_error("PLONK Prove: transcript challenge lies in H");

        // Round 4
        proof.a_zeta = evaluate_polynomial(a_coefficients, zeta);
        proof.b_zeta = evaluate_polynomial(b_coefficients, zeta);
        proof.c_zeta = evaluate_polynomial(c_coefficients, zeta);
        proof.sigma_1_zeta = evaluate_polynomial(key.fixed.sigma_1.coefficients, zeta);
        proof.sigma_2_zeta = evaluate_polynomial(key.fixed.sigma_2.coefficients, zeta);
        proof.z_zeta_omega = evaluate_polynomial(z_coefficients, zeta_omega);
        transcript.append(proof.a_zeta);
        transcript.append(proof.b_zeta);
        transcript.append(proof.c_zeta);
        transcript.append(proof.sigma_1_zeta);
        transcript.append(proof.sigma_2_zeta);
        transcript.append(proof.z_zeta_omega);
        const Scalar batching_challenge = transcript.challenge();

        // Round 5
        const Scalar identity_at_zeta = (proof.a_zeta + beta * zeta + gamma) * (proof.b_zeta + beta * key.domain.k_1 * zeta + gamma) * (proof.c_zeta + beta * key.domain.k_2 * zeta + gamma);
        const Scalar sigma_12_at_zeta = (proof.a_zeta + beta * proof.sigma_1_zeta + gamma) * (proof.b_zeta + beta * proof.sigma_2_zeta + gamma);
        const Scalar l_1_zeta = z_h_zeta * n_inverse * (zeta - Scalar::one()).inverse();
        std::vector<Scalar> p_non_constant;
        add_scaled(p_non_constant, key.fixed.q_l.coefficients, proof.a_zeta);
        add_scaled(p_non_constant, key.fixed.q_r.coefficients, proof.b_zeta);
        add_scaled(p_non_constant, key.fixed.q_m.coefficients, proof.a_zeta * proof.b_zeta);
        add_scaled(p_non_constant, key.fixed.q_o.coefficients, proof.c_zeta);
        add_scaled(p_non_constant, key.fixed.q_c.coefficients, Scalar::one());
        add_scaled(p_non_constant, z_coefficients, alpha * identity_at_zeta);
        add_scaled(p_non_constant, key.fixed.sigma_3.coefficients, -alpha * sigma_12_at_zeta * beta * proof.z_zeta_omega);
        add_scaled(p_non_constant, z_coefficients, alpha_squared * l_1_zeta);
        proof.p_non_constant_zeta = evaluate_polynomial(p_non_constant, zeta);

        const Scalar zeta_to_part = zeta ^ part_size;
        const Scalar zeta_to_two_parts = zeta_to_part.squared();
        std::vector<Scalar> t_partial;
        add_scaled(t_partial, t_lo, Scalar::one());
        add_scaled(t_partial, t_mid, zeta_to_part);
        add_scaled(t_partial, t_hi, zeta_to_two_parts);
        proof.t_zeta = evaluate_polynomial(t_partial, zeta);

        std::vector<Scalar> batched_polynomial = t_partial;
        Scalar power = batching_challenge;
        add_scaled(batched_polynomial, p_non_constant, power); power *= batching_challenge;
        add_scaled(batched_polynomial, a_coefficients, power); power *= batching_challenge;
        add_scaled(batched_polynomial, b_coefficients, power); power *= batching_challenge;
        add_scaled(batched_polynomial, c_coefficients, power); power *= batching_challenge;
        add_scaled(batched_polynomial, key.fixed.sigma_1.coefficients, power); power *= batching_challenge;
        add_scaled(batched_polynomial, key.fixed.sigma_2.coefficients, power);

        power = batching_challenge;
        Scalar batched_value = proof.t_zeta + power * proof.p_non_constant_zeta; power *= batching_challenge;
        batched_value += power * proof.a_zeta; power *= batching_challenge;
        batched_value += power * proof.b_zeta; power *= batching_challenge;
        batched_value += power * proof.c_zeta; power *= batching_challenge;
        batched_value += power * proof.sigma_1_zeta; power *= batching_challenge;
        batched_value += power * proof.sigma_2_zeta;

        const std::vector<Scalar> w_zeta = synthetic_division(std::move(batched_polynomial), zeta, batched_value);
        const std::vector<Scalar> w_zeta_omega = synthetic_division(z_coefficients, zeta_omega, proof.z_zeta_omega);
        proof.w_zeta = commit_polynomial(w_zeta, key.srs);
        proof.w_zeta_omega = commit_polynomial(w_zeta_omega, key.srs);
    }
};

} // namespace

std::unique_ptr<Backend> make_cpu_backend()
{
    return std::make_unique<CpuBackend>();
}

} // namespace nkr::plonk
