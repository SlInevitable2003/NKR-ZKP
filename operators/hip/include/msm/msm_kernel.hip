#pragma once

#include "msm/msm_common.hip"

#define DCU_MSM_BLOCK_SIZE 256
#define DCU_MSM_BLOCKS_PER_CU 3

// CUDA assigns buckets dynamically to warps and uses a warp broadcast for the
// task id. DCU uses a static grid-stride assignment instead, so every lane
// derives the same bucket id without relying on warp collectives.
template <typename AffT, typename XYZZT>
__global__ __launch_bounds__(DCU_MSM_BLOCK_SIZE, DCU_MSM_BLOCKS_PER_CU)
void dcu_intra_bucket_accumulation(
    const uint32_t *bucket_offsets, const uint32_t *indices,
    const AffT *points, XYZZT *bucket_sums_per_lane,
    uint32_t buckets_count, uint32_t windows_count, uint32_t scale,
    uint32_t last_window_buckets_count, uint32_t low_windows_count,
    uint32_t low_window)
{
    constexpr uint32_t warp_size = 32;
    constexpr uint32_t warps_per_block = DCU_MSM_BLOCK_SIZE / warp_size;
    const uint32_t lane = threadIdx.x % warp_size;
    const uint32_t warp_in_block = threadIdx.x / warp_size;
    const uint32_t first_bucket =
        blockIdx.x * warps_per_block + warp_in_block;
    const uint32_t bucket_stride = gridDim.x * warps_per_block;
    const uint32_t valid_buckets_count = buckets_count - 1;

    for (uint32_t bucket = first_bucket; bucket < valid_buckets_count;
         bucket += bucket_stride) {
        XYZZT sum;
        sum.inf();
        const AffT *window_points = points;
        const uint32_t current_windows_count =
            windows_count - (bucket >= last_window_buckets_count - 1);
        for (uint32_t window = low_window; window < current_windows_count;
             window += low_windows_count, window_points += scale) {
            const uint32_t start =
                bucket_offsets[window * buckets_count + bucket];
            const uint32_t end =
                bucket_offsets[window * buckets_count + bucket + 1];
            for (uint32_t position = start + lane; position < end;
                 position += warp_size) {
                AffT point = window_points[
                    indices[window * scale + position]];
                sum.pacc(point);
            }
        }
        bucket_sums_per_lane[
            low_window * buckets_count * warp_size +
            bucket * warp_size + lane] = sum;
    }
}

template <typename XYZZT>
__global__ void dcu_msm_lane_reduce(
    const XYZZT *bucket_sums_per_lane, XYZZT *bucket_sums,
    uint32_t buckets_count, uint32_t low_windows_count)
{
    constexpr uint32_t warp_size = 32;
    const uint32_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t valid_buckets_count = buckets_count - 1;
    const uint32_t valid_threads = low_windows_count * valid_buckets_count;
    if (tid >= valid_threads) return;

    const uint32_t bucket = tid % valid_buckets_count;
    const uint32_t window = tid / valid_buckets_count;
    XYZZT sum;
    sum.inf();
    const XYZZT *partials =
        bucket_sums_per_lane + window * buckets_count * warp_size +
        bucket * warp_size;
    for (uint32_t lane = 0; lane < warp_size; ++lane) sum.add(partials[lane]);
    bucket_sums[window * buckets_count + bucket] = sum;
}

template <typename ProjT, typename XYZZT, size_t BlockSize>
__global__ __launch_bounds__(BlockSize) void dcu_msm_bucket_reduce(
    XYZZT *bucket_sums, ProjT *window_sums, uint32_t buckets_count)
{
    const uint32_t valid_buckets_count = buckets_count - 1;
    const uint32_t window = blockIdx.x;
    const uint32_t lane = threadIdx.x;
    uint32_t buckets_per_thread = buckets_count / BlockSize;
    buckets_per_thread += buckets_per_thread == 0;

    const uint32_t start = lane * buckets_per_thread;
    uint32_t end = (lane + 1) * buckets_per_thread;
    end = end < valid_buckets_count ? end : valid_buckets_count;

    XYZZT sum;
    XYZZT scan;
    sum.inf();
    scan.inf();
    for (int bucket = static_cast<int>(end) - 1;
         bucket >= static_cast<int>(start); --bucket) {
        sum.add(bucket_sums[window * buckets_count + bucket]);
        scan.add(sum);
    }

    __shared__ XYZZT shared[BlockSize];
    shared[lane] = scan;
    __syncthreads();

    for (uint32_t offset = 1; offset < BlockSize; offset <<= 1) {
        scan = shared[lane ^ offset];
        __syncthreads();
        shared[lane].add(scan);
        __syncthreads();
    }

    scan = shared[lane];
    scan.to_jacobian();
    __syncthreads();
    if (lane == 0)
        window_sums[window] = *reinterpret_cast<ProjT *>(&scan);
    __syncthreads();

    shared[BlockSize - 1 - lane] = sum;
    __syncthreads();
    for (uint32_t offset = 1; offset < BlockSize; offset <<= 1) {
        if (lane >= offset)
            scan = shared[lane - offset];
        else
            scan.inf();
        __syncthreads();
        shared[lane].add(scan);
        __syncthreads();
    }

    if (lane == 0) shared[BlockSize - 1].inf();
    __syncthreads();
    for (uint32_t offset = 1; offset < BlockSize; offset <<= 1) {
        scan = shared[lane ^ offset];
        __syncthreads();
        shared[lane].add(scan);
        __syncthreads();
    }

    sum = shared[lane];
    sum.to_jacobian();
    __syncthreads();
    if (lane == 0) {
        ProjT correction = *reinterpret_cast<ProjT *>(&sum);
        for (uint32_t i = 1; i < buckets_per_thread; i <<= 1)
            correction.dbl();
        window_sums[window].add(correction);
    }
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
