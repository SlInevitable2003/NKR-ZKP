#pragma once

#include "msm/msm_common.hip"

template <typename FieldT, typename AffT, typename ProjT>
__global__ void dcu_window_msm(
    const FieldT *scalars,
    const AffT *bases,
    ProjT *window_sums,
    size_t scale,
    size_t window_bits,
    size_t windows_count)
{
    const size_t window = blockIdx.x;
    if (window >= windows_count) return;
    extern __shared__ __align__(16) unsigned char shared_bytes[];
    auto *partial = reinterpret_cast<ProjT *>(shared_bytes);
    ProjT local;
    local.inf();
    for (size_t i = threadIdx.x; i < scale; i += blockDim.x) {
        const uint32_t key = get_window_by_ptr(
            scalars + i, static_cast<uint32_t>(window * window_bits),
            static_cast<uint32_t>(window_bits));
        if (key == 0) continue;
        ProjT multiple;
        ProjT power;
        power = bases[i];
        multiple.inf();
        uint32_t remaining = key;
        while (remaining != 0) {
            if (remaining & 1U) multiple.add(power);
            remaining >>= 1;
            if (remaining != 0) power.dbl();
        }
        local.add(multiple);
    }
    partial[threadIdx.x] = local;
    __syncthreads();
    for (size_t stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (threadIdx.x < stride) partial[threadIdx.x].add(partial[threadIdx.x + stride]);
        __syncthreads();
    }
    if (threadIdx.x == 0) window_sums[window] = partial[0];
}

template <typename FieldT, typename AffT, typename ProjT>
__global__ void dcu_fixed_base_msm(
    const AffT *base, const FieldT *scalars, ProjT *out, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    FieldT scalar = scalars[i];
    scalar.from();
    ProjT result;
    result.inf();
    for (int bit = static_cast<int>(FieldT::nbits) - 1; bit >= 0; --bit) {
        result.dbl();
        if (get_bit(scalar, static_cast<size_t>(bit))) result.add(base[0]);
    }
    out[i] = result;
}
