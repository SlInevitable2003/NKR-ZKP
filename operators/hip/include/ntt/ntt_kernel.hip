#pragma once

#include "ntt/ntt_common.hip"

template <typename FieldT>
__global__ void bit_reverse_copy(
    const FieldT *input, FieldT *output, uint32_t rounds, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) output[bit_rev(i, rounds)] = input[i];
}

template <typename FieldT>
__global__ void radix2_ntt_stage(
    const FieldT *input, FieldT *output, const FieldT *omegas,
    size_t count, size_t span)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count / 2) return;
    const size_t half = span / 2;
    const size_t group = i / half;
    const size_t offset = i % half;
    const size_t left = group * span + offset;
    const FieldT u = input[left];
    const FieldT v = input[left + half] * omegas[offset * (count / span)];
    output[left] = u + v;
    output[left + half] = u - v;
}

template <typename FieldT>
__global__ void multiply_table_kernel(
    FieldT *values, const FieldT *factors, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) values[i] *= factors[i];
}

template <typename FieldT>
__global__ void multiply_constant_kernel(
    FieldT *values, const FieldT *factor, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) values[i] *= factor[0];
}
