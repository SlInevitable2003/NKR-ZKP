#include "plonk/protocol.hpp"

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
        "PLONK is a structural scaffold; protocol semantics are not implemented yet");
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
    Proof proof;
    backend.prove(proving_key, witness, proof);
    return proof;
}

bool verify(const VerificationKey &,
            const std::vector<Scalar> &,
            const Proof &)
{
    throw_scaffold_only();
}

} // namespace nkr::plonk
