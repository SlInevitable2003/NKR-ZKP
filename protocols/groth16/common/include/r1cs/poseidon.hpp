#pragma once

#include <array>
#include <cstddef>
#include <map>

#include "crypto/poseidon.hpp"
#include "r1cs/r1cs.hpp"

namespace LightZK::Poseidon {

using nkr::poseidon::FULL_ROUNDS;
using nkr::poseidon::PARTIAL_ROUNDS;
using nkr::poseidon::ROUND_CONSTANTS;
using nkr::poseidon::TOTAL_ROUNDS;
using nkr::poseidon::WIDTH;

template<typename S>
std::array<S, WIDTH> permute(const std::array<S, WIDTH> &state)
{
    return nkr::poseidon::permute<S>(state);
}

template<typename S>
S hash2(const S &left, const S &right)
{
    return nkr::poseidon::hash2<S>(left, right);
}

// Constrains digest = Poseidon([0, left, right])[0]. The caller owns the
// visibility and assignments of left, right and digest. All intermediate
// variables allocated here are private.
template<typename S>
void add_hash2_constraints(R1CSManager<S> &manager,
                           std::map<std::size_t, S> &assignment,
                           const Variable<S> &left,
                           const S &left_value,
                           const Variable<S> &right,
                           const S &right_value,
                           const Variable<S> &digest)
{
    const auto &parameters = nkr::poseidon::detail::parameters<S>();
    std::array<LinearCombination<S>, WIDTH> state{
        LinearCombination<S>(S::zero()),
        LinearCombination<S>(left),
        LinearCombination<S>(right),
    };
    std::array<S, WIDTH> state_values{S::zero(), left_value, right_value};
    std::size_t constant = 0;
    const std::size_t first_partial = FULL_ROUNDS / 2;
    const std::size_t last_partial = first_partial + PARTIAL_ROUNDS;

    for (std::size_t round = 0; round < TOTAL_ROUNDS; ++round) {
        std::array<LinearCombination<S>, WIDTH> sboxed = state;
        std::array<S, WIDTH> sboxed_values = state_values;

        for (std::size_t i = 0; i < WIDTH; ++i) {
            state[i] += LinearCombination<S>(parameters.round_constants[constant]);
            state_values[i] += parameters.round_constants[constant++];
        }

        const bool full_round = round < first_partial || round >= last_partial;
        const std::size_t sboxes = full_round ? WIDTH : 1;
        for (std::size_t i = 0; i < sboxes; ++i) {
            Variable<S> squared(VariableType::Private, manager);
            Variable<S> fourth(VariableType::Private, manager);
            Variable<S> fifth(VariableType::Private, manager);
            const S squared_value = state_values[i] * state_values[i];
            const S fourth_value = squared_value * squared_value;
            const S fifth_value = fourth_value * state_values[i];

            manager.add_constraint(squared, state[i], state[i]);
            manager.add_constraint(fourth, squared, squared);
            manager.add_constraint(fifth, fourth, state[i]);
            assignment[squared.get_id()] = squared_value;
            assignment[fourth.get_id()] = fourth_value;
            assignment[fifth.get_id()] = fifth_value;
            sboxed[i] = LinearCombination<S>(fifth);
            sboxed_values[i] = fifth_value;
        }
        for (std::size_t i = sboxes; i < WIDTH; ++i) {
            sboxed[i] = state[i];
            sboxed_values[i] = state_values[i];
        }

        std::array<LinearCombination<S>, WIDTH> next_state;
        std::array<S, WIDTH> next_values{S::zero(), S::zero(), S::zero()};
        for (std::size_t row = 0; row < WIDTH; ++row) {
            LinearCombination<S> mixed;
            for (std::size_t column = 0; column < WIDTH; ++column) {
                mixed += sboxed[column] * parameters.mds[row][column];
                next_values[row] +=
                    parameters.mds[row][column] * sboxed_values[column];
            }
            Variable<S> output(VariableType::Private, manager);
            manager.add_constraint(output, mixed);
            assignment[output.get_id()] = next_values[row];
            next_state[row] = LinearCombination<S>(output);
        }
        state = next_state;
        state_values = next_values;
    }

    manager.add_constraint(LinearCombination<S>(digest), state[0]);
}

} // namespace LightZK::Poseidon
