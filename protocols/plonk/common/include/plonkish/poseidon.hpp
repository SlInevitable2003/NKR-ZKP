#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "plonkish/arithmetization.hpp"
#include "crypto/poseidon.hpp"

namespace Plonkish::Poseidon {

// Constrains digest = Poseidon([0, left, right])[0] and returns the digest as a
// new public variable. left and right are existing variables (typically
// private); all round intermediate variables are private.
inline Variable add_hash2(PlonkishManager &manager, Variable left, Variable right)
{
    using nkr::plonk::Scalar;
    const auto &parameters = nkr::poseidon::detail::parameters<Scalar>();
    const Scalar digest_value =
        nkr::poseidon::hash2<Scalar>(manager.value(left), manager.value(right));
    const Variable digest = manager.public_variable(digest_value);

    std::array<Variable, nkr::poseidon::WIDTH> state{
        manager.constant(Scalar::zero()),
        left,
        right,
    };
    std::size_t constant = 0;
    const std::size_t first_partial = nkr::poseidon::FULL_ROUNDS / 2;
    const std::size_t last_partial =
        first_partial + nkr::poseidon::PARTIAL_ROUNDS;

    for (std::size_t round = 0; round < nkr::poseidon::TOTAL_ROUNDS; ++round) {
        for (std::size_t i = 0; i < nkr::poseidon::WIDTH; ++i) {
            state[i] = manager.linear_combination(
                {{Scalar::one(), state[i]}}, parameters.round_constants[constant++]);
        }

        const bool full_round = round < first_partial || round >= last_partial;
        const std::size_t sboxes = full_round ? nkr::poseidon::WIDTH : 1;
        for (std::size_t i = 0; i < sboxes; ++i) {
            const Variable squared = manager.multiply(state[i], state[i]);
            const Variable fourth = manager.multiply(squared, squared);
            state[i] = manager.multiply(fourth, state[i]);
        }

        std::array<Variable, nkr::poseidon::WIDTH> mixed;
        for (std::size_t row = 0; row < nkr::poseidon::WIDTH; ++row) {
            mixed[row] = manager.linear_combination({
                {parameters.mds[row][0], state[0]},
                {parameters.mds[row][1], state[1]},
                {parameters.mds[row][2], state[2]},
            });
        }
        state = mixed;
    }

    manager.assert_equal(digest, state[0]);
    return digest;
}

} // namespace Plonkish::Poseidon
