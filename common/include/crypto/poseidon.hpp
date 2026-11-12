#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>

#include <gmp.h>

#include "libff/algebra/fields/bigint.hpp"

namespace nkr::poseidon {

/*
    Poseidon permutation shared by the BN254 circuits: width 3, x^5 S-box,
    8 full rounds and 57 partial rounds. The round constants and MDS matrix
    live in poseidon_constants.inc and were generated for alt_bn128's scalar
    field with the reference Grain LFSR parameter generator.
*/
constexpr std::size_t WIDTH = 3;
constexpr std::size_t FULL_ROUNDS = 8;
constexpr std::size_t PARTIAL_ROUNDS = 57;
constexpr std::size_t TOTAL_ROUNDS = FULL_ROUNDS + PARTIAL_ROUNDS;
constexpr std::size_t ROUND_CONSTANTS = WIDTH * TOTAL_ROUNDS;

namespace detail {

inline constexpr const char *ROUND_CONSTANT_HEX[] = {
#define POSEIDON_RC(value) #value,
#define POSEIDON_MDS_ROW(a, b, c)
#include "poseidon_constants.inc"
#undef POSEIDON_MDS_ROW
#undef POSEIDON_RC
};

inline constexpr const char *MDS_HEX[WIDTH][WIDTH] = {
#define POSEIDON_RC(value)
#define POSEIDON_MDS_ROW(a, b, c) {#a, #b, #c},
#include "poseidon_constants.inc"
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

} // namespace nkr::poseidon
