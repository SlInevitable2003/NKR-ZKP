#include "plonk/protocol.hpp"
#include "plonk/transcript.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"

namespace nkr::plonk {
namespace {

bool is_power_of_two(std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

template <typename Field>
bool field_is_well_formed(const Field &value)
{
    return mpn_cmp(value.mont_repr.data, Field::mod.data, Field::num_limbs) < 0;
}

bool scalar_is_well_formed(const Scalar &value)
{
    return field_is_well_formed(value);
}

bool g1_is_valid(const G1 &point)
{
    return field_is_well_formed(point.X) && field_is_well_formed(point.Y) &&
           field_is_well_formed(point.Z) && point.is_well_formed() &&
           (Scalar::mod * point).is_zero();
}

bool g2_is_valid(const G2 &point)
{
    return field_is_well_formed(point.X.c0) && field_is_well_formed(point.X.c1) &&
           field_is_well_formed(point.Y.c0) && field_is_well_formed(point.Y.c1) &&
           field_is_well_formed(point.Z.c0) && field_is_well_formed(point.Z.c1) &&
           point.is_well_formed() && (Scalar::mod * point).is_zero();
}

Scalar public_input_evaluation(const std::vector<Scalar> &public_input,
                               const DomainParameters &domain,
                               const Scalar &zeta,
                               const Scalar &z_h)
{
    const Scalar n_inverse =
        Scalar(static_cast<long>(domain.domain_size)).inverse();
    Scalar root = Scalar::one();
    Scalar result = Scalar::zero();
    for (const Scalar &value : public_input) {
        if (!value.is_zero())
            result += value * root * z_h * n_inverse * (zeta - root).inverse();
        root *= domain.omega;
    }
    return result;
}

template <typename T>
bool require_size(const std::vector<T> &column,
                  std::size_t expected,
                  const char *name,
                  std::string &error)
{
    if (column.size() != expected) {
        error = std::string(name) + " must contain exactly " +
                std::to_string(expected) + " entries";
        return false;
    }
    return true;
}

} // namespace

[[noreturn]] void throw_scaffold_only()
{
    throw std::logic_error(
        "the requested PLONK backend operation is not implemented yet");
}

UniversalSrs generate_srs(const std::size_t max_degree)
{
    Scalar tau;
    do {
        tau = Scalar::random_element();
    } while (tau == Scalar::zero() || tau == Scalar::one());

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

bool validate_circuit(const Circuit &circuit, std::string &error)
{
    const std::size_t n = circuit.domain_size;
    if (!is_power_of_two(n)) {
        error = "domain size must be a non-zero power of two";
        return false;
    }
    if (circuit.rows > n) {
        error = "circuit row count exceeds the domain size";
        return false;
    }
    if (circuit.public_inputs > circuit.rows) {
        error = "public input count exceeds the circuit row count";
        return false;
    }

    if (!require_size(circuit.q_l, n, "q_l", error) ||
        !require_size(circuit.q_r, n, "q_r", error) ||
        !require_size(circuit.q_m, n, "q_m", error) ||
        !require_size(circuit.q_o, n, "q_o", error) ||
        !require_size(circuit.q_c, n, "q_c", error) ||
        !require_size(circuit.sigma_1, n, "sigma_1", error) ||
        !require_size(circuit.sigma_2, n, "sigma_2", error) ||
        !require_size(circuit.sigma_3, n, "sigma_3", error)) {
        return false;
    }

    if (n > std::numeric_limits<std::uint32_t>::max() / 3) {
        error = "encoded permutation does not fit in uint32_t";
        return false;
    }

    std::vector<bool> seen(3 * n, false);
    const std::array<const std::vector<std::uint32_t> *, 3> sigma = {
        &circuit.sigma_1, &circuit.sigma_2, &circuit.sigma_3};
    for (const auto *column : sigma) {
        for (const std::uint32_t destination : *column) {
            if (destination >= 3 * n) {
                error = "permutation cell is outside the circuit";
                return false;
            }
            if (seen[destination]) {
                error = "wiring map is not a permutation";
                return false;
            }
            seen[destination] = true;
        }
    }
    return true;
}

bool validate_srs(const UniversalSrs &srs, std::size_t domain_size, std::string &error)
{
    if (srs.g1_powers.size() < domain_size + 3) {
        error = "SRS must contain powers [tau^0] through [tau^(N+2)]";
        return false;
    }
    for (std::size_t i = 0; i < domain_size + 3; ++i) {
        if (srs.g1_powers[i].is_zero() || !srs.g1_powers[i].is_well_formed()) {
            error = "SRS contains an invalid G1 power";
            return false;
        }
    }
    if (srs.g2.is_zero() || srs.tau_g2.is_zero() ||
        !srs.g2.is_well_formed() || !srs.tau_g2.is_well_formed()) {
        error = "SRS contains an invalid G2 point";
        return false;
    }
    if (srs.g1_powers.front() != G1::one() || srs.g2 != G2::one()) {
        error = "SRS generators do not match the protocol generators";
        return false;
    }

    // Batch all adjacent power relations with a random linear combination:
    // e(sum r^i [tau^(i+1)]_1, [1]_2) ==
    // e(sum r^i [tau^i]_1, [tau]_2).
    const std::size_t relations = domain_size + 2;
    Scalar challenge;
    do {
        challenge = Scalar::random_element();
    } while (challenge.is_zero());
    std::vector<Scalar> weights(relations);
    weights[0] = Scalar::one();
    for (std::size_t i = 1; i < relations; ++i)
        weights[i] = weights[i - 1] * challenge;

    const G1 lower = libff::multi_exp<G1, Scalar, libff::multi_exp_method_BDLO12>(
        srs.g1_powers.begin(),
        srs.g1_powers.begin() + relations,
        weights.begin(),
        weights.end(),
        1);
    const G1 upper = libff::multi_exp<G1, Scalar, libff::multi_exp_method_BDLO12>(
        srs.g1_powers.begin() + 1,
        srs.g1_powers.begin() + 1 + relations,
        weights.begin(),
        weights.end(),
        1);
    if (Curve::reduced_pairing(upper, srs.g2) !=
        Curve::reduced_pairing(lower, srs.tau_g2)) {
        error = "SRS G1 powers are inconsistent with tau_g2";
        return false;
    }
    return true;
}

bool validate_proving_key(const ProvingKey &key, std::string &error)
{
    if (!validate_circuit(key.circuit, error)) return false;
    const std::size_t n = key.circuit.domain_size;
    const std::size_t m = key.domain.quotient_domain_size;
    if (key.domain.domain_size != n || !is_power_of_two(m) || m < 3 * n + 6) {
        error = "proving key contains invalid domain sizes";
        return false;
    }
    if ((key.domain.omega ^ n) != Scalar::one() ||
        (n > 1 && (key.domain.omega ^ (n / 2)) == Scalar::one())) {
        error = "proving key contains an invalid N-th root of unity";
        return false;
    }
    if (key.domain.k_1.is_zero() || key.domain.k_2.is_zero() ||
        (key.domain.k_1 ^ n) == Scalar::one() ||
        (key.domain.k_2 ^ n) == Scalar::one() ||
        ((key.domain.k_2 * key.domain.k_1.inverse()) ^ n) == Scalar::one()) {
        error = "proving key wire cosets are not disjoint";
        return false;
    }
    if (key.domain.coset_shift.is_zero() ||
        (key.domain.coset_shift ^ m) == Scalar::one()) {
        error = "proving key quotient coset intersects its evaluation domain";
        return false;
    }
    const std::array<const PolynomialData *, 8> fixed = {
        &key.fixed.q_l, &key.fixed.q_r, &key.fixed.q_m, &key.fixed.q_o,
        &key.fixed.q_c, &key.fixed.sigma_1, &key.fixed.sigma_2,
        &key.fixed.sigma_3};
    for (const PolynomialData *polynomial : fixed) {
        if (polynomial->evaluations.size() != n ||
            polynomial->coefficients.size() != n ||
            polynomial->coset_evaluations.size() != m) {
            error = "proving key contains an invalid fixed polynomial";
            return false;
        }
    }
    return validate_srs(key.srs, n, error);
}

bool validate_witness(const ProvingKey &key,
                      const Witness &witness,
                      std::string &error)
{
    const std::size_t n = key.domain.domain_size;
    if (witness.a.size() != n || witness.b.size() != n || witness.c.size() != n ||
        witness.public_input.size() != n) {
        error = "witness columns must contain exactly N entries";
        return false;
    }
    for (std::size_t row = 0; row < n; ++row) {
        const Scalar residual = key.circuit.q_l[row] * witness.a[row] +
                                key.circuit.q_r[row] * witness.b[row] +
                                key.circuit.q_m[row] * witness.a[row] * witness.b[row] +
                                key.circuit.q_o[row] * witness.c[row] +
                                key.circuit.q_c[row] + witness.public_input[row];
        if (residual != Scalar::zero()) {
            error = "witness violates arithmetic row " + std::to_string(row);
            return false;
        }
    }
    return true;
}

void setup(Backend &backend,
           const Circuit &circuit,
           const UniversalSrs &srs,
           ProvingKey &proving_key,
           VerificationKey &verification_key)
{
    std::string error;
    if (!validate_circuit(circuit, error))
        throw std::invalid_argument("PLONK Setup: " + error);
    if (!validate_srs(srs, circuit.domain_size, error))
        throw std::invalid_argument("PLONK Setup: " + error);
    backend.setup(circuit, srs, proving_key, verification_key);
}

Proof prove(Backend &backend,
            const ProvingKey &proving_key,
            const Witness &witness)
{
    std::string error;
    if (!validate_proving_key(proving_key, error))
        throw std::invalid_argument("PLONK Prove: " + error);
    if (!validate_witness(proving_key, witness, error))
        throw std::invalid_argument("PLONK Prove: " + error);
    Proof proof;
    backend.prove(proving_key, witness, proof);
    return proof;
}

bool verify(const VerificationKey &key,
            const std::vector<Scalar> &public_input,
            const Proof &proof)
{
    try {
        const std::size_t n = key.domain.domain_size;
        if (!is_power_of_two(n) || public_input.size() != n ||
            (key.domain.omega ^ n) != Scalar::one() ||
            (n > 1 && (key.domain.omega ^ (n / 2)) == Scalar::one()) ||
            key.domain.k_1.is_zero() || key.domain.k_2.is_zero() ||
            (key.domain.k_1 ^ n) == Scalar::one() ||
            (key.domain.k_2 ^ n) == Scalar::one() ||
            ((key.domain.k_2 * key.domain.k_1.inverse()) ^ n) == Scalar::one() ||
            key.g2 != G2::one() || key.tau_g2.is_zero() || !g2_is_valid(key.g2) ||
            !g2_is_valid(key.tau_g2)) {
            return false;
        }
        if (!scalar_is_well_formed(key.domain.omega) ||
            !scalar_is_well_formed(key.domain.k_1) ||
            !scalar_is_well_formed(key.domain.k_2)) {
            return false;
        }

        const std::array<const G1 *, 8> key_points = {
            &key.commitments.q_l, &key.commitments.q_r, &key.commitments.q_m,
            &key.commitments.q_o, &key.commitments.q_c, &key.commitments.sigma_1,
            &key.commitments.sigma_2, &key.commitments.sigma_3};
        for (const G1 *point : key_points) {
            if (!g1_is_valid(*point)) return false;
        }
        const std::array<const G1 *, 9> proof_points = {
            &proof.a_commitment, &proof.b_commitment, &proof.c_commitment,
            &proof.z_commitment, &proof.t_lo_commitment, &proof.t_mid_commitment,
            &proof.t_hi_commitment, &proof.w_zeta, &proof.w_zeta_omega};
        for (const G1 *point : proof_points) {
            if (!g1_is_valid(*point)) return false;
        }
        const std::array<const Scalar *, 8> proof_scalars = {
            &proof.a_zeta, &proof.b_zeta, &proof.c_zeta,
            &proof.sigma_1_zeta, &proof.sigma_2_zeta,
            &proof.z_zeta_omega, &proof.p_non_constant_zeta, &proof.t_zeta};
        for (const Scalar *value : proof_scalars) {
            if (!scalar_is_well_formed(*value)) return false;
        }
        for (const Scalar &value : public_input) {
            if (!scalar_is_well_formed(value)) return false;
        }

        Transcript transcript = initialize_transcript(key, public_input);
        transcript.append(proof.a_commitment);
        transcript.append(proof.b_commitment);
        transcript.append(proof.c_commitment);
        const Scalar beta = transcript.challenge();
        const Scalar gamma = transcript.challenge();
        transcript.append(proof.z_commitment);
        const Scalar alpha = transcript.challenge();
        transcript.append(proof.t_lo_commitment);
        transcript.append(proof.t_mid_commitment);
        transcript.append(proof.t_hi_commitment);
        const Scalar zeta = transcript.challenge();
        transcript.append(proof.a_zeta);
        transcript.append(proof.b_zeta);
        transcript.append(proof.c_zeta);
        transcript.append(proof.sigma_1_zeta);
        transcript.append(proof.sigma_2_zeta);
        transcript.append(proof.z_zeta_omega);
        const Scalar batching_challenge = transcript.challenge();

        const Scalar z_h = (zeta ^ n) - Scalar::one();
        if (z_h.is_zero()) return false;
        const Scalar n_inverse = Scalar(static_cast<long>(n)).inverse();
        const Scalar l_1 = z_h * n_inverse * (zeta - Scalar::one()).inverse();
        const Scalar public_input_zeta =
            public_input_evaluation(public_input, key.domain, zeta, z_h);
        const Scalar identity =
            (proof.a_zeta + beta * zeta + gamma) *
            (proof.b_zeta + beta * key.domain.k_1 * zeta + gamma) *
            (proof.c_zeta + beta * key.domain.k_2 * zeta + gamma);
        const Scalar sigma_12 =
            (proof.a_zeta + beta * proof.sigma_1_zeta + gamma) *
            (proof.b_zeta + beta * proof.sigma_2_zeta + gamma);
        const Scalar alpha_squared = alpha.squared();
        const Scalar p_constant =
            public_input_zeta - alpha * sigma_12 *
                (proof.c_zeta + gamma) * proof.z_zeta_omega -
            alpha_squared * l_1;
        if (proof.p_non_constant_zeta + p_constant != z_h * proof.t_zeta)
            return false;

        G1 p_non_constant_commitment =
            proof.a_zeta * key.commitments.q_l +
            proof.b_zeta * key.commitments.q_r +
            (proof.a_zeta * proof.b_zeta) * key.commitments.q_m +
            proof.c_zeta * key.commitments.q_o + key.commitments.q_c +
            (alpha * identity) * proof.z_commitment -
            (alpha * sigma_12 * beta * proof.z_zeta_omega) *
                key.commitments.sigma_3 +
            (alpha_squared * l_1) * proof.z_commitment;

        const std::size_t part_size = n + 2;
        const Scalar zeta_to_part = zeta ^ part_size;
        const G1 t_partial_commitment =
            proof.t_lo_commitment + zeta_to_part * proof.t_mid_commitment +
            zeta_to_part.squared() * proof.t_hi_commitment;

        Scalar power = batching_challenge;
        G1 batched_commitment = t_partial_commitment +
                                power * p_non_constant_commitment;
        Scalar batched_value = proof.t_zeta +
                               power * proof.p_non_constant_zeta;
        power *= batching_challenge;
        batched_commitment = batched_commitment + power * proof.a_commitment;
        batched_value += power * proof.a_zeta;
        power *= batching_challenge;
        batched_commitment = batched_commitment + power * proof.b_commitment;
        batched_value += power * proof.b_zeta;
        power *= batching_challenge;
        batched_commitment = batched_commitment + power * proof.c_commitment;
        batched_value += power * proof.c_zeta;
        power *= batching_challenge;
        batched_commitment = batched_commitment + power * key.commitments.sigma_1;
        batched_value += power * proof.sigma_1_zeta;
        power *= batching_challenge;
        batched_commitment = batched_commitment + power * key.commitments.sigma_2;
        batched_value += power * proof.sigma_2_zeta;

        const G1 generator = G1::one();
        const bool first_opening =
            Curve::reduced_pairing(batched_commitment - batched_value * generator,
                                   key.g2) ==
            Curve::reduced_pairing(proof.w_zeta,
                                   key.tau_g2 - zeta * key.g2);
        if (!first_opening) return false;
        return Curve::reduced_pairing(
                   proof.z_commitment - proof.z_zeta_omega * generator,
                   key.g2) ==
               Curve::reduced_pairing(proof.w_zeta_omega,
                                      key.tau_g2 -
                                          (zeta * key.domain.omega) * key.g2);
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace nkr::plonk
