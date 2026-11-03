#pragma once

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "fields/bucket.hip"
#include "msm/msm_kernel.hip"
#include "utils.hip"

// HIP port of the CUDA bucketed Pippenger MSM. The only scheduling change is
// static warp-to-bucket assignment in dcu_intra_bucket_accumulation, which
// avoids the unreliable DCU warp shuffle path.
template <typename FieldT, typename AffT, typename ProjT, typename XYZZT,
          typename HostFT, typename HostPT, bool is_g2 = false,
          size_t instances = 1>
class MSMContext {
    static constexpr size_t elastic = 2;

    AffT *bases[instances]{};
    XYZZT *bucket_sums_per_lane = nullptr;
    XYZZT *bucket_sums = nullptr;
    ProjT *window_sums = nullptr;
    TypedGpuArena arena;
    int compute_units = 1;

public:
    const size_t scale;
    const size_t window_bits;
    const size_t windows_count;
    const size_t buckets_count;
    const size_t low_windows_count;

    MSMContext(size_t scale_, size_t window_bits_)
        : scale(scale_), window_bits(window_bits_),
          windows_count(ceil_div(FieldT::nbits, window_bits_)),
          buckets_count(size_t{1} << window_bits_),
          low_windows_count(ceil_div(windows_count, elastic))
    {
        static_assert(sizeof(ProjT) == sizeof(HostPT),
                      "device and host projective layouts must match");
        for (size_t i = 0; i < instances; ++i)
            arena.register_alloc(bases[i], elastic * scale);
        arena.register_alloc(bucket_sums_per_lane,
                             low_windows_count * buckets_count * 32);
        arena.register_alloc(bucket_sums,
                             low_windows_count * buckets_count);
        arena.register_alloc(window_sums, low_windows_count);
        arena.commit(is_g2 ? "DCU G2 MSMContext" : "DCU G1 MSMContext");

        int device = 0;
        hipDeviceProp_t properties{};
        HIP_CHECK(hipGetDevice(&device));
        HIP_CHECK(hipGetDeviceProperties(&properties, device));
        compute_units = std::max(1, properties.multiProcessorCount);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());
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

        const size_t doubling_offset = low_windows_count * window_bits;
        for (size_t copy = 1; copy < elastic; ++copy) {
            AffT *current = bases[instance_id] + copy * scale;
            const AffT *previous = bases[instance_id] + (copy - 1) * scale;
            HIP_CHECK(hipMemcpy(current, previous, scale * sizeof(AffT),
                                hipMemcpyDeviceToDevice));
            dcu_lift_bases<<<ceil_div(scale, size_t{256}), 256>>>(
                current, scale, doubling_offset);
            HIP_CHECK(hipDeviceSynchronize());
            HIP_CHECK(hipGetLastError());
        }
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

        hipStream_t stream;
        HIP_CHECK(hipStreamCreate(&stream));
        const size_t last_window_bits =
            FieldT::nbits % window_bits == 0
                ? window_bits
                : FieldT::nbits % window_bits;
        const size_t last_window_buckets_count =
            size_t{1} << last_window_bits;
        const size_t valid_buckets_count = buckets_count - 1;

        const int accumulation_blocks =
            compute_units * DCU_MSM_BLOCKS_PER_CU;
        for (size_t low_window = 0; low_window < low_windows_count;
             ++low_window) {
            dcu_intra_bucket_accumulation<AffT, XYZZT>
                <<<accumulation_blocks, DCU_MSM_BLOCK_SIZE, 0, stream>>>(
                    bucket_context.buckets_off,
                    bucket_context.indices_as_vals,
                    bases[instance_id], bucket_sums_per_lane,
                    static_cast<uint32_t>(buckets_count),
                    static_cast<uint32_t>(windows_count),
                    static_cast<uint32_t>(scale),
                    static_cast<uint32_t>(last_window_buckets_count),
                    static_cast<uint32_t>(low_windows_count),
                    static_cast<uint32_t>(low_window));
        }
        HIP_CHECK(hipGetLastError());

        const size_t lane_reduce_jobs =
            low_windows_count * valid_buckets_count;
        dcu_msm_lane_reduce<<<ceil_div(lane_reduce_jobs, size_t{256}),
                              256, 0, stream>>>(
            bucket_sums_per_lane, bucket_sums,
            static_cast<uint32_t>(buckets_count),
            static_cast<uint32_t>(low_windows_count));
        HIP_CHECK(hipGetLastError());

        constexpr size_t reduction_block = 256 / XYZZT::degree;
        dcu_msm_bucket_reduce<ProjT, XYZZT, reduction_block>
            <<<low_windows_count, reduction_block, 0, stream>>>(
                bucket_sums, window_sums,
                static_cast<uint32_t>(buckets_count));
        HIP_CHECK(hipStreamSynchronize(stream));
        HIP_CHECK(hipGetLastError());
        HIP_CHECK(hipStreamDestroy(stream));

        if (result) {
            std::vector<HostPT> sums(low_windows_count);
            HIP_CHECK(hipMemcpy(sums.data(), window_sums,
                                low_windows_count * sizeof(ProjT),
                                hipMemcpyDeviceToHost));
            HostPT total = HostPT::zero();
            for (size_t window = low_windows_count; window-- > 0;) {
                for (size_t bit = 0; bit < window_bits; ++bit)
                    total = total.dbl();
                total = total + sums[window];
            }
            *result = total;
        }
    }

private:
    static __global__ void dcu_lift_bases(
        AffT *points, size_t count, size_t doubling_offset)
    {
        const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= count) return;
        ProjT point = points[i];
        for (size_t bit = 0; bit < doubling_offset; ++bit) point.dbl();
        points[i] = point;
    }
};
