#pragma once

#include "fields/alt_bn128-fp2.hip"
#include "curves/jacobian_t.hip"

namespace alt_bn128 {
using g1_t = jacobian_t<fp_t>;
using g2_t = jacobian_t<fp2_t>;
} // namespace alt_bn128

template <class FieldT>
__device__ inline bool get_bit(const FieldT &scalar, size_t bit)
{
    const auto *words = reinterpret_cast<const uint32_t *>(&scalar);
    return ((words[bit / 32] >> (bit % 32)) & 1U) != 0;
}

template <class FieldT>
__host__ __device__ inline uint32_t get_window(
    const FieldT &scalar, uint32_t offset, uint32_t window_bits)
{
    const uint32_t top_word = sizeof(FieldT) / sizeof(uint32_t) - 1;
    const auto *words = reinterpret_cast<const uint32_t *>(&scalar);
    const uint32_t word = offset / 32;
    const uint32_t shift = offset % 32;
    const uint64_t lo = words[word];
    const uint64_t hi = word < top_word ? words[word + 1] : 0;
    return static_cast<uint32_t>(((lo | (hi << 32)) >> shift) &
                                 ((uint64_t{1} << window_bits) - 1));
}

template <class FieldT>
__device__ inline uint32_t get_window_by_ptr(
    const FieldT *scalar, uint32_t offset, uint32_t window_bits)
{
    return get_window(*scalar, offset, window_bits);
}
