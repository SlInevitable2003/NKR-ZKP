#pragma once

#include <cstring>
#include <stdexcept>
#include <vector>

#include "fields/bucket.hip"
#include "msm/msm_kernel.hip"
#include "utils.hip"

// Correctness-first windowed MSM for DCU. Every window is handled by one block;
// threads compute independent partial sums and reduce through shared memory.
// This deliberately avoids atomics on curve points, warp shuffle, hipCUB, and
// cooperative groups. It is a stable baseline that can later be tiled without
// changing the public operator boundary.
template <typename FieldT, typename AffT, typename ProjT, typename XYZZT,
          typename HostFT, typename HostPT, bool is_g2 = false,
          size_t instances = 1>
class MSMContext {
    AffT *bases[instances]{};
    ProjT *window_sums = nullptr;
    TypedGpuArena arena;

public:
    const size_t scale;
    const size_t window_bits;
    const size_t windows_count;
    const size_t buckets_count;

    MSMContext(size_t scale_, size_t window_bits_)
        : scale(scale_), window_bits(window_bits_),
          windows_count(ceil_div(FieldT::nbits, window_bits_)),
          buckets_count(size_t{1} << window_bits_)
    {
        static_assert(sizeof(ProjT) == sizeof(HostPT),
                      "device and host projective layouts must match");
        for (size_t i = 0; i < instances; ++i)
            arena.register_alloc(bases[i], scale);
        arena.register_alloc(window_sums, windows_count);
        arena.commit(is_g2 ? "DCU G2 MSMContext" : "DCU G1 MSMContext");
    }

    void load_bases(const HostPT *host_bases, bool = true,
                    size_t instance_id = 0)
    {
        if (instance_id >= instances)
            throw std::out_of_range("MSMContext instance id");
        std::vector<AffT> buffer(scale);
        std::memset(buffer.data(), 0, buffer.size() * sizeof(AffT));
        #pragma omp parallel for
        for (size_t i = 0; i < scale; ++i) {
            if (!host_bases[i].is_zero())
                std::memcpy(&buffer[i], &host_bases[i], sizeof(AffT));
        }
        HIP_CHECK(hipMemcpy(bases[instance_id], buffer.data(),
                            scale * sizeof(AffT), hipMemcpyHostToDevice));
    }

    void msm(BucketContext<FieldT, HostFT> &bucket_context,
             HostPT *result = nullptr, size_t instance_id = 0)
    {
        if (instance_id >= instances)
            throw std::out_of_range("MSMContext instance id");
        if (bucket_context.scale != scale ||
            bucket_context.window_bits != window_bits)
            throw std::invalid_argument("MSMContext/BucketContext mismatch");
        bucket_context.process();

        constexpr size_t block = 256;
        dcu_window_msm<FieldT, AffT, ProjT>
            <<<windows_count, block, block * sizeof(ProjT)>>>(
                bucket_context.scalars, bases[instance_id], window_sums,
                scale, window_bits, windows_count);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());

        if (result) {
            std::vector<HostPT> sums(windows_count);
            HIP_CHECK(hipMemcpy(sums.data(), window_sums,
                                windows_count * sizeof(ProjT),
                                hipMemcpyDeviceToHost));
            HostPT total = HostPT::zero();
            for (size_t window = windows_count; window-- > 0;) {
                for (size_t bit = 0; bit < window_bits; ++bit)
                    total = total.dbl();
                total = total + sums[window];
            }
            *result = total;
        }
    }
};
