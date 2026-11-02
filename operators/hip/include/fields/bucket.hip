#pragma once

#include "msm/msm_common.hip"
#include "utils.hip"

// Scalar staging for the DCU-safe MSM. Scalars are converted out of Montgomery
// form once; the MSM extracts windows directly and does not rely on hipCUB,
// warp shuffles, or cooperative groups.
template <typename FieldT, typename HostFT>
class BucketContext {
    TypedGpuArena arena;
    bool canonical = false;

    static __global__ void canonicalize(FieldT *values, size_t count)
    {
        const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i < count) values[i].from();
    }

public:
    FieldT *scalars = nullptr;
    const size_t scale;
    const size_t window_bits;
    const size_t windows_count;
    const size_t buckets_count;

    BucketContext(size_t scale_, size_t window_bits_)
        : scale(scale_), window_bits(window_bits_),
          windows_count(ceil_div(FieldT::nbits, window_bits_)),
          buckets_count(size_t{1} << window_bits_)
    {
        arena.register_alloc(scalars, scale);
        arena.commit("DCU BucketContext");
    }

    void load_scalars(const HostFT *host_scalars)
    {
        HIP_CHECK(hipMemcpy(scalars, host_scalars, scale * sizeof(FieldT),
                            hipMemcpyHostToDevice));
        canonical = false;
    }

    void load_scalars(const FieldT *device_scalars)
    {
        HIP_CHECK(hipMemcpy(scalars, device_scalars, scale * sizeof(FieldT),
                            hipMemcpyDeviceToDevice));
        canonical = false;
    }

    void process(bool = true, bool = true, bool = true, bool = true)
    {
        if (canonical) return;
        constexpr size_t block = 256;
        canonicalize<<<ceil_div(scale, block), block>>>(scalars, scale);
        HIP_CHECK(hipGetLastError());
        canonical = true;
    }
};
