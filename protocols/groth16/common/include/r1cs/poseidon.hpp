#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <stdexcept>

#include <gmp.h>

#include "libff/algebra/fields/bigint.hpp"
#include "r1cs/r1cs.hpp"

namespace LightZK::Poseidon {

constexpr std::size_t WIDTH = 3;
constexpr std::size_t FULL_ROUNDS = 8;
constexpr std::size_t PARTIAL_ROUNDS = 57;
constexpr std::size_t TOTAL_ROUNDS = FULL_ROUNDS + PARTIAL_ROUNDS;
constexpr std::size_t ROUND_CONSTANTS = WIDTH * TOTAL_ROUNDS;

namespace detail {

inline constexpr const char *ROUND_CONSTANT_HEX[] = {
#define POSEIDON_RC(value) #value,
#define POSEIDON_MDS_ROW(a, b, c)
#include "r1cs/poseidon_constants.inc"
#undef POSEIDON_MDS_ROW
#undef POSEIDON_RC
};

inline constexpr const char *MDS_HEX[WIDTH][WIDTH] = {
#define POSEIDON_RC(value)
#define POSEIDON_MDS_ROW(a, b, c) {#a, #b, #c},
#include "r1cs/poseidon_constants.inc"
#undef POSEIDON_MDS_ROW
#undef POSEIDON_RC
};

static_assert(sizeof(ROUND_CONSTANT_HEX) / sizeof(ROUND_CONSTANT_HEX[0]) ==
              ROUND_CONSTANTS);

template<typename S>
S field_from_hex(const char *text)
{
    mpz_t value;
    if (mpz_init_set_str(value, text, 0) != 0)
        throw std::invalid_argument("invalid Poseidon field constant");
    const libff::bigint<S::num_limbs> integer(value);
    mpz_clear(value);
    return S(integer);
}

template<typename S>
struct Parameters {
    std::array<S, ROUND_CONSTANTS> round_constants;
    std::array<std::array<S, WIDTH>, WIDTH> mds;
};

template<typename S>
const Parameters<S> &parameters()
{
    static const Parameters<S> result = [] {
        Parameters<S> parameters;
        for (std::size_t i = 0; i < ROUND_CONSTANTS; ++i)
            parameters.round_constants[i] = field_from_hex<S>(ROUND_CONSTANT_HEX[i]);
        for (std::size_t row = 0; row < WIDTH; ++row)
            for (std::size_t column = 0; column < WIDTH; ++column)
                parameters.mds[row][column] = field_from_hex<S>(MDS_HEX[row][column]);
        return parameters;
    }();
    return result;
}

template<typename S>
S sbox(const S &value)
{
    const S squared = value * value;
    return squared * squared * value;
}

} // namespace detail

template<typename S>
std::array<S, WIDTH> permute(std::array<S, WIDTH> state)
{
    const auto &parameters = detail::parameters<S>();
    std::size_t constant = 0;
    const std::size_t first_partial = FULL_ROUNDS / 2;
    const std::size_t last_partial = first_partial + PARTIAL_ROUNDS;

    for (std::size_t round = 0; round < TOTAL_ROUNDS; ++round) {
        for (std::size_t i = 0; i < WIDTH; ++i)
            state[i] += parameters.round_constants[constant++];

        if (round < first_partial || round >= last_partial) {
            for (S &word : state) word = detail::sbox(word);
        } else {
            state[0] = detail::sbox(state[0]);
        }

        std::array<S, WIDTH> mixed{S::zero(), S::zero(), S::zero()};
        for (std::size_t row = 0; row < WIDTH; ++row)
            for (std::size_t column = 0; column < WIDTH; ++column)
                mixed[row] += parameters.mds[row][column] * state[column];
        state = mixed;
    }
    return state;
}

template<typename S>
S hash2(const S &left, const S &right)
{
    return permute<S>({S::zero(), left, right})[0];
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
    const auto &parameters = detail::parameters<S>();
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
