#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include <thrust/execution_policy.h>
#include <thrust/for_each.h>
#include <thrust/sort.h>

#include "msm/msm_common.hip"
#include "utils.hip"

template <typename FieldT, typename HostFT, typename key_t = uint16_t>
class BucketContext {
    TypedGpuArena arena;
    bool canonical = false;
    bool bucketed = false;

    static __global__ void canonicalize(FieldT *values, size_t count)
    {
        const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i < count) values[i].from();
    }

    static __global__ void extract_windows(
        const FieldT *values, key_t *keys, uint32_t *indices, size_t scale,
        size_t window_bits, size_t windows_count)
    {
        const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= scale) return;
        for (size_t window = 0; window < windows_count; ++window) {
            keys[window * scale + i] = static_cast<key_t>(get_window_by_ptr(
                values + i, static_cast<uint32_t>(window * window_bits),
                static_cast<uint32_t>(window_bits)));
            indices[window * scale + i] = static_cast<uint32_t>(i);
        }
    }

    static __global__ void scatter_offsets(
        const key_t *keys, uint32_t *offsets, size_t scale,
        size_t windows_count, size_t buckets_count, size_t log_scale)
    {
        const size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
        if (tid >= windows_count * buckets_count) return;

        const size_t window = tid / buckets_count;
        const key_t *window_keys = keys + window * scale;
        const key_t bucket = static_cast<key_t>(tid % buckets_count);
        uint32_t left = 0;
        uint32_t right = static_cast<uint32_t>(scale);
        if (window_keys[0] > bucket) {
            right = 0;
        } else {
            for (size_t bit = log_scale; bit > 0; --bit) {
                const uint32_t middle = left + ((right - left) >> 1);
                const bool above = window_keys[middle] > bucket;
                left = above ? left : middle;
                right = above ? middle : right;
            }
        }
        offsets[tid] = right;
    }

public:
    FieldT *scalars = nullptr;
    key_t *window_scalars_as_keys = nullptr;
    uint32_t *indices_as_vals = nullptr;
    uint32_t *buckets_size = nullptr;
    uint32_t *buckets_off = nullptr;

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
        arena.register_alloc(window_scalars_as_keys, windows_count * scale);
        arena.register_alloc(indices_as_vals, windows_count * scale);
        arena.register_alloc(buckets_size, windows_count * buckets_count);
        arena.register_alloc(buckets_off, windows_count * buckets_count);
        arena.commit("DCU BucketContext");
    }

    void load_scalars(const HostFT *host_scalars)
    {
        HIP_CHECK(hipMemcpy(scalars, host_scalars, scale * sizeof(FieldT),
                            hipMemcpyHostToDevice));
        canonical = false;
        bucketed = false;
    }

    void load_scalars(const FieldT *device_scalars)
    {
        HIP_CHECK(hipMemcpy(scalars, device_scalars, scale * sizeof(FieldT),
                            hipMemcpyDeviceToDevice));
        canonical = false;
        bucketed = false;
    }

    void process(bool from = true, bool extraction = true,
                 bool sort = true, bool scatter = true)
    {
        constexpr size_t block = 256;
        if (from && !canonical) {
            canonicalize<<<ceil_div(scale, block), block>>>(scalars, scale);
            HIP_CHECK(hipGetLastError());
            HIP_CHECK(hipDeviceSynchronize());
            canonical = true;
        }

        if (bucketed || !(extraction || sort || scatter)) return;
        if (!canonical) {
            canonicalize<<<ceil_div(scale, block), block>>>(scalars, scale);
            HIP_CHECK(hipGetLastError());
            HIP_CHECK(hipDeviceSynchronize());
            canonical = true;
        }

        std::vector<hipStream_t> streams(windows_count);
        for (auto &stream : streams) HIP_CHECK(hipStreamCreate(&stream));
        std::vector<decltype(thrust::hip::par.on(streams[0]))> policies;
        policies.reserve(windows_count);
        for (auto stream : streams)
            policies.push_back(thrust::hip::par.on(stream));

        extract_windows<<<ceil_div(scale, block), block, 0, streams[0]>>>(
            scalars, window_scalars_as_keys, indices_as_vals, scale,
            window_bits, windows_count);
        HIP_CHECK(hipGetLastError());
        HIP_CHECK(hipDeviceSynchronize());

        #pragma omp parallel for num_threads(4)
        for (size_t window = 0; window < windows_count; ++window) {
            thrust::sort_by_key(
                policies[window],
                window_scalars_as_keys + window * scale,
                window_scalars_as_keys + (window + 1) * scale,
                indices_as_vals + window * scale);
        }
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());

        assert((scale & (scale - 1)) == 0 &&
               "MSM scale must be a power of two");
        const size_t log_scale = static_cast<size_t>(__builtin_ctzll(scale));
        scatter_offsets<<<ceil_div(windows_count * buckets_count, block),
                          block>>>(
            window_scalars_as_keys, buckets_off, scale, windows_count,
            buckets_count, log_scale);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());

        for (auto stream : streams) HIP_CHECK(hipStreamDestroy(stream));
        bucketed = true;
    }
};
