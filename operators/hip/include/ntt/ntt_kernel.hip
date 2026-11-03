#pragma once

#include "ntt/ntt_common.hip"

constexpr size_t DCU_NTT_BLOCK_SIZE = 1024;

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

// These three kernels are the HIP counterpart of the CUDA on-chip NTT path.
// CUDA uses warp shuffles for the final five butterfly levels. DCU keeps the
// same decomposition and indexing, but exchanges every level through LDS so
// the implementation has no dependency on wavefront-local shuffle semantics.
template <typename FieldT>
__global__ void dcu_onchip_butterfly_transformation_1(
    const FieldT *input, FieldT *output, const FieldT *omegas, size_t count)
{
    uint32_t round = 0;
    const size_t offset = threadIdx.x;
    const size_t batch = blockIdx.x;
    size_t index = offset * gridDim.x + batch;
    size_t remaining = count;

    __shared__ FieldT shared[DCU_NTT_BLOCK_SIZE];
    FieldT coefficient = input[index];

    for (uint32_t adjacent = DCU_NTT_BLOCK_SIZE >> 1;
         adjacent != 0; adjacent >>= 1, ++round) {
        shared[offset] = coefficient;
        __syncthreads();

        index &= ~(remaining >>= 1);
        const FieldT other = shared[offset ^ adjacent];
        coefficient = (offset & adjacent) == 0
            ? coefficient + other
            : (other - coefficient) * omegas[index << round];
        __syncthreads();
    }

    output[batch * DCU_NTT_BLOCK_SIZE + offset] = coefficient;
}

template <typename FieldT>
__global__ void dcu_onchip_butterfly_transformation_2(
    const FieldT *input, FieldT *output, const FieldT *omegas, size_t count)
{
    uint32_t round = 10;
    const size_t offset = threadIdx.x;
    const size_t batch = blockIdx.x;
    size_t index = offset * gridDim.x + batch;
    size_t remaining = count >> 10;

    __shared__ FieldT shared[DCU_NTT_BLOCK_SIZE];
    FieldT coefficient = input[index];
    index >>= 10;

    for (uint32_t adjacent = DCU_NTT_BLOCK_SIZE >> 1;
         adjacent != 0; adjacent >>= 1, ++round) {
        shared[offset] = coefficient;
        __syncthreads();

        index &= ~(remaining >>= 1);
        const FieldT other = shared[offset ^ adjacent];
        coefficient = (offset & adjacent) == 0
            ? coefficient + other
            : (other - coefficient) * omegas[index << round];
        __syncthreads();
    }

    output[batch * DCU_NTT_BLOCK_SIZE + offset] = coefficient;
}

template <typename FieldT>
__global__ void dcu_onchip_butterfly_transformation_3(
    const FieldT *input, FieldT *output, const FieldT *omegas,
    size_t count, size_t batch_size)
{
    uint32_t round = 20;
    const size_t thread = blockIdx.x * blockDim.x + threadIdx.x;
    const size_t offset = thread % batch_size;
    const size_t batch = thread / batch_size;
    size_t index = offset * (count / batch_size) + batch;
    size_t remaining = count >> 20;

    extern __shared__ FieldT shared[];
    FieldT coefficient = input[index];
    index >>= 20;

    for (size_t adjacent = batch_size >> 1;
         adjacent != 0; adjacent >>= 1, ++round) {
        shared[threadIdx.x] = coefficient;
        __syncthreads();

        index &= ~(remaining >>= 1);
        const FieldT other = shared[threadIdx.x ^ adjacent];
        coefficient = (offset & adjacent) == 0
            ? coefficient + other
            : (other - coefficient) * omegas[index << round];
        __syncthreads();
    }

    output[batch * batch_size + offset] = coefficient;
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
