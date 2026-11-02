#pragma once

#include <algorithm>
#include <cstddef>

#include "utils.hip"

namespace dcu {

template <typename T, typename Op>
__global__ void scan_step_kernel(
    const T *input, T *output, size_t count, size_t offset, Op op)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    output[i] = i < offset ? input[i] : op(input[i - offset], input[i]);
}

// Global ping-pong inclusive scan. It uses O(log n) ordinary kernels instead
// of warp intrinsics. The implementation is intentionally uniform across DCU
// wavefront sizes and supports non-commutative associative operations.
template <typename T>
class ScanContext {
    T *scratch[2]{};
    size_t capacity;
    TypedGpuArena arena;

public:
    explicit ScanContext(size_t max_count) : capacity(std::max<size_t>(1, max_count))
    {
        arena.register_alloc(scratch[0], capacity);
        arena.register_alloc(scratch[1], capacity);
        arena.commit("DCU ScanContext");
    }

    template <typename Op>
    void inclusive(const T *input, T *output, size_t count, Op op, const T &)
    {
        if (count == 0) return;
        if (count > capacity) throw std::out_of_range("ScanContext capacity");
        HIP_CHECK(hipMemcpy(scratch[0], input, count * sizeof(T),
                            hipMemcpyDeviceToDevice));
        const T *source = scratch[0];
        T *destination = scratch[1];
        constexpr size_t block = 256;
        for (size_t offset = 1; offset < count; offset <<= 1) {
            scan_step_kernel<<<ceil_div(count, block), block>>>(
                source, destination, count, offset, op);
            HIP_CHECK(hipGetLastError());
            const T *next_source = destination;
            destination = const_cast<T *>(source);
            source = next_source;
        }
        HIP_CHECK(hipMemcpy(output, source, count * sizeof(T),
                            hipMemcpyDeviceToDevice));
    }
};

} // namespace dcu
