#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "mgpu/CuScan.cuh"
#include "utils/arena.cuh"
#include "utils/arith.cuh"
#include "utils/cuda_check.cuh"
#include "utils/kernel.cuh"

template <typename DeviceT, typename HostT>
DeviceT device_value(const HostT &value)
{
    static_assert(sizeof(DeviceT) == sizeof(HostT),
                  "host and device values must have identical layouts");
    static_assert(std::is_trivially_copyable<DeviceT>::value,
                  "device value must be trivially copyable");
    // Host field objects are not guaranteed to have DeviceT's stronger
    // alignment.  Reinterpreting a key member directly made nvcc emit aligned
    // SIMD loads from an unaligned address on x86.  Copy through aligned local
    // storage while preserving the common Montgomery representation.
    alignas(DeviceT) unsigned char storage[sizeof(DeviceT)];
    std::memcpy(storage, &value, sizeof(DeviceT));
    return *reinterpret_cast<const DeviceT *>(storage);
}

template <typename FieldT>
struct FieldMultiply {
    __device__ FieldT operator()(const FieldT &left, const FieldT &right) const
    {
        return left * right;
    }
};

template <typename FieldT>
__global__ void pointwise_inverse_kernel(FieldT *values, std::size_t count)
{
    const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    // reciprocal() exchanges data between adjacent lanes.  Every lane in the
    // final block must therefore participate even when it has no output.
    const FieldT value = i < count ? values[i] : FieldT::one();
    const FieldT inverse = value.reciprocal();
    if (i < count) values[i] = inverse;
}

template <typename FieldT>
void pointwise_inverse(FieldT *values, std::size_t count)
{
    if (count == 0) return;
    constexpr std::size_t block_size = 256;
    pointwise_inverse_kernel<<<ceil_div(count, block_size), block_size>>>(values, count);
    CUDA_CHECK(cudaGetLastError());
}

template <typename FieldT, std::size_t terms_per_thread>
__global__ void block_horner_kernel(const FieldT *coefficients,
                                    std::size_t coefficient_count,
                                    FieldT point,
                                    FieldT *partial_values)
{
    const std::size_t chunk = blockIdx.x * blockDim.x + threadIdx.x;
    const std::size_t chunk_count =
        (coefficient_count + terms_per_thread - 1) / terms_per_thread;
    if (chunk >= chunk_count) return;

    const std::size_t begin = chunk * terms_per_thread;
    const std::size_t proposed_end = begin + terms_per_thread;
    const std::size_t end =
        proposed_end < coefficient_count ? proposed_end : coefficient_count;
    FieldT value = coefficients[end - 1];
    for (std::size_t i = end - 1; i > begin; --i)
        value = value * point + coefficients[i - 1];
    partial_values[chunk] = value;
}

template <typename FieldT, typename HostFT, std::size_t terms_per_thread = 64>
class PolynomialEvaluationContext {
    FieldT *scratch[2];
    TypedGpuArena arena;

public:
    explicit PolynomialEvaluationContext(std::size_t max_coefficients)
    {
        const std::size_t partials =
            std::max<std::size_t>(1, ceil_div(max_coefficients, terms_per_thread));
        arena.register_alloc(scratch[0], partials);
        arena.register_alloc(scratch[1], partials);
        arena.commit("PolynomialEvaluationContext");
    }

    HostFT evaluate(const FieldT *coefficients,
                    std::size_t coefficient_count,
                    const HostFT &point)
    {
        if (coefficient_count == 0) return HostFT::zero();

        const FieldT *source = coefficients;
        FieldT *destination = scratch[0];
        std::size_t count = coefficient_count;
        HostFT stage_point = point;
        unsigned stage = 0;
        constexpr std::size_t block_size = 256;
        while (count > 1) {
            const std::size_t partials = ceil_div(count, terms_per_thread);
            block_horner_kernel<FieldT, terms_per_thread>
                <<<ceil_div(partials, block_size), block_size>>>(
                    source, count, device_value<FieldT>(stage_point), destination);
            CUDA_CHECK(cudaGetLastError());
            source = destination;
            destination = scratch[(++stage) & 1U];
            count = partials;
            stage_point = stage_point ^ terms_per_thread;
        }

        HostFT result;
        CUDA_CHECK(cudaMemcpy(&result, source, sizeof(FieldT), cudaMemcpyDeviceToHost));
        return result;
    }
};

template <typename FieldT>
struct AffineTransform {
    FieldT scale;
    FieldT offset;
};

template <typename FieldT>
struct AffineCompose {
    __device__ AffineTransform<FieldT> operator()(
        const AffineTransform<FieldT> &prefix,
        const AffineTransform<FieldT> &next) const
    {
        // The scan follows coefficient order from highest to lowest.  Appending
        // next means applying next after the accumulated prefix.
        return {next.scale * prefix.scale,
                next.scale * prefix.offset + next.offset};
    }
};

template <typename FieldT>
__global__ void make_synthetic_transforms_kernel(
    const FieldT *polynomial,
    std::size_t quotient_size,
    FieldT point,
    AffineTransform<FieldT> *transforms)
{
    const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= quotient_size) return;
    FieldT zero;
    zero.zero();
    transforms[i].scale = i == 0 ? zero : point;
    transforms[i].offset = polynomial[quotient_size - i];
}

template <typename FieldT>
__global__ void extract_synthetic_quotient_kernel(
    const AffineTransform<FieldT> *transforms,
    FieldT *quotient,
    std::size_t quotient_size)
{
    const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < quotient_size)
        quotient[quotient_size - 1 - i] = transforms[i].offset;
}

template <typename FieldT, typename HostFT>
class SyntheticDivisionContext {
    AffineTransform<FieldT> *transforms;
    mgpu::CuScanContext<AffineTransform<FieldT>> scan;
    TypedGpuArena arena;

public:
    explicit SyntheticDivisionContext(std::size_t max_coefficients)
        : scan(std::max<std::size_t>(1, max_coefficients - 1))
    {
        arena.register_alloc(transforms,
                             std::max<std::size_t>(1, max_coefficients - 1));
        arena.commit("SyntheticDivisionContext");
    }

    void divide(const FieldT *polynomial,
                std::size_t coefficient_count,
                const HostFT &point,
                FieldT *quotient)
    {
        if (coefficient_count < 2) return;
        const std::size_t quotient_size = coefficient_count - 1;
        constexpr std::size_t block_size = 256;
        const FieldT device_point = device_value<FieldT>(point);
        make_synthetic_transforms_kernel<<<ceil_div(quotient_size, block_size),
                                           block_size>>>(
            polynomial, quotient_size, device_point, transforms);

        const AffineTransform<FieldT> identity = {
            device_value<FieldT>(HostFT::one()),
            device_value<FieldT>(HostFT::zero())};
        scan.inclusive(transforms, transforms, quotient_size,
                       AffineCompose<FieldT>{}, identity);
        extract_synthetic_quotient_kernel<<<ceil_div(quotient_size, block_size),
                                            block_size>>>(
            transforms, quotient, quotient_size);
        CUDA_CHECK(cudaGetLastError());
    }
};
