#pragma once
#include <cstdint>

#include "mgpu/utils/CuLoadStore.cuh"
#include "utils/arena.cuh"
#include "utils/arith.cuh"
#include "utils/cuda_check.cuh"

namespace mgpu {
    #define SCAN_BLOCK_SIZE 256
    #define SCAN_GRAIN_SIZE 4

    enum class CuScanType { Inclusive, Exclusive };

    template <typename T, typename Op, CuScanType scan_type, size_t block_size = SCAN_BLOCK_SIZE>
    __device__ void CuScan_block(T &x, T *dbuffer, Op op, T neutral = 0) {
        size_t head = 0;
        const uint32_t tid = threadIdx.x;
        dbuffer[tid] = x;
        __syncthreads();
        
        if (block_size >= 2) { if (tid >= 1) { x = op(dbuffer[head + tid - 1], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 4) { if (tid >= 2) { x = op(dbuffer[head + tid - 2], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 8) { if (tid >= 4) { x = op(dbuffer[head + tid - 4], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 16) { if (tid >= 8) { x = op(dbuffer[head + tid - 8], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 32) { if (tid >= 16) { x = op(dbuffer[head + tid - 16], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 64) { if (tid >= 32) { x = op(dbuffer[head + tid - 32], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 128) { if (tid >= 64) { x = op(dbuffer[head + tid - 64], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 256) { if (tid >= 128) { x = op(dbuffer[head + tid - 128], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }
        if (block_size >= 512) { if (tid >= 256) { x = op(dbuffer[head + tid - 256], x); } head = block_size - head; dbuffer[head + tid] = x; __syncthreads(); }

        if (scan_type == CuScanType::Exclusive) {
            dbuffer[tid] = x;
            __syncthreads();
            x = (tid == 0) ? neutral : dbuffer[tid - 1];
        }
    }

    template <typename T, typename Op, size_t block_size = SCAN_BLOCK_SIZE, size_t grain_size = SCAN_GRAIN_SIZE>
    __global__ void CuScan(T *in, size_t len, Op op, T neutral = 0) { // Inclusive
        __shared__ T sdata[2 * block_size];

        T reg[grain_size];
        GlobalToReg<T, grain_size>(in, len, reg, neutral);
        #pragma unroll
        for (size_t j = 1; j < grain_size; j++) reg[j] = op(reg[j - 1], reg[j]);
        
        T total = reg[grain_size - 1];
        CuScan_block<T, Op, CuScanType::Exclusive>(total, sdata, op, neutral);
        #pragma unroll
        for (size_t j = 0; j < grain_size; j++) reg[j] = op(total, reg[j]);

        RegToGlobal<T, grain_size>(reg, in, len);
    }

    template <typename T, typename Op, size_t block_size = SCAN_BLOCK_SIZE, size_t grain_size = SCAN_GRAIN_SIZE>
    __global__ void CuScan_add(T *in, T* out, T *aux, size_t len, Op op, T neutral = 0) { // Inclusive
        __shared__ T sdata[2 * block_size];

        T reg[grain_size];
        GlobalToReg<T, grain_size>(in, len, reg, neutral);
        #pragma unroll
        for (size_t j = 1; j < grain_size; j++) reg[j] = op(reg[j - 1], reg[j]);
        
        T total = reg[grain_size - 1];
        CuScan_block<T, Op, CuScanType::Exclusive>(total, sdata, op, neutral);
        #pragma unroll
        for (size_t j = 0; j < grain_size; j++) reg[j] = op(total, reg[j]);

        T extra = (blockIdx.x == 0) ? neutral : aux[blockIdx.x - 1];
        #pragma unroll
        for (size_t j = 0; j < grain_size; j++) reg[j] = op(extra, reg[j]);

        RegToGlobal<T, grain_size>(reg, out, len);
    }

    template <typename T, typename Op, size_t block_size = SCAN_BLOCK_SIZE,
              size_t grain_size = SCAN_GRAIN_SIZE>
    __global__ void CuScan_totals(const T *in, T *totals, size_t len,
                                  Op op, T neutral)
    {
        __shared__ T sdata[2 * block_size];
        T reg[grain_size];
        GlobalToReg<T, grain_size>(in, len, reg, neutral);
        #pragma unroll
        for (size_t j = 1; j < grain_size; ++j)
            reg[j] = op(reg[j - 1], reg[j]);

        T total = reg[grain_size - 1];
        CuScan_block<T, Op, CuScanType::Inclusive>(total, sdata, op, neutral);
        if (threadIdx.x + 1 == block_size) totals[blockIdx.x] = total;
    }

    template <typename T, size_t block_size = SCAN_BLOCK_SIZE,
              size_t grain_size = SCAN_GRAIN_SIZE>
    class CuScanContext {
        static constexpr size_t items_per_block = block_size * grain_size;

        T *scratch;
        TypedGpuArena arena;

        static size_t scratch_size(size_t count)
        {
            size_t result = 0;
            while (count > items_per_block) {
                count = ceil_div(count, items_per_block);
                result += count;
            }
            return result == 0 ? 1 : result;
        }

        template <typename Op>
        void inclusive_impl(const T *input, T *output, size_t count,
                            T *level_scratch, Op op, T neutral)
        {
            const size_t blocks = ceil_div(count, items_per_block);
            if (blocks == 1) {
                CuScan_add<T, Op, block_size, grain_size><<<1, block_size>>>(
                    const_cast<T *>(input), output, nullptr, count, op, neutral);
                return;
            }

            CuScan_totals<T, Op, block_size, grain_size><<<blocks, block_size>>>(
                input, level_scratch, count, op, neutral);
            inclusive_impl(level_scratch, level_scratch, blocks,
                           level_scratch + blocks, op, neutral);
            CuScan_add<T, Op, block_size, grain_size><<<blocks, block_size>>>(
                const_cast<T *>(input), output, level_scratch, count, op, neutral);
        }

    public:
        explicit CuScanContext(size_t max_count)
        {
            arena.register_alloc(scratch, scratch_size(max_count));
            arena.commit("CuScanContext");
        }

        template <typename Op>
        void inclusive(const T *input, T *output, size_t count, Op op, T neutral)
        {
            if (count == 0) return;
            inclusive_impl(input, output, count, scratch, op, neutral);
            CUDA_CHECK(cudaGetLastError());
        }
    };
} // namespace mgpu
