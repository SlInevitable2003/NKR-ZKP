#pragma once

#include "msm/msm_kernel.hip"

template <typename FieldT, typename AffT, typename ProjT>
__global__ void fixmsm_kernel(
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
