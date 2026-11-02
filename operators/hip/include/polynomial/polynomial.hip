#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "utils.hip"
#include "utils/scan.hip"

template <typename DeviceT, typename HostT>
DeviceT device_value(const HostT &value)
{
    static_assert(sizeof(DeviceT) == sizeof(HostT));
    static_assert(std::is_trivially_copyable<DeviceT>::value);
    alignas(DeviceT) unsigned char storage[sizeof(DeviceT)];
    std::memcpy(storage, &value, sizeof(DeviceT));
    return *reinterpret_cast<const DeviceT *>(storage);
}

template <typename FieldT>
struct FieldMultiply {
    __device__ FieldT operator()(const FieldT &a, const FieldT &b) const
    { return a * b; }
};

template <typename FieldT>
__device__ FieldT dcu_field_inverse(const FieldT &value)
{
    if (value.is_zero()) {
        FieldT zero;
        zero.zero();
        return zero;
    }
    FieldT result = FieldT::one();
    for (int bit = static_cast<int>(FieldT::nbits) - 1; bit >= 0; --bit) {
        result = result ^ 2;
        const size_t limb = static_cast<size_t>(bit) / 32;
        uint32_t exponent_limb = FieldT::modulus_limb(limb);
        if (limb == 0) exponent_limb -= 2;
        if ((exponent_limb >> (bit % 32)) & 1U) result *= value;
    }
    return result;
}

template <typename FieldT>
__global__ void pointwise_inverse_kernel(FieldT *values, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) values[i] = dcu_field_inverse(values[i]);
}

template <typename FieldT>
void pointwise_inverse(FieldT *values, size_t count)
{
    if (count == 0) return;
    constexpr size_t block = 256;
    pointwise_inverse_kernel<<<ceil_div(count, block), block>>>(values, count);
    HIP_CHECK(hipGetLastError());
}

template <typename FieldT, size_t terms_per_thread>
__global__ void block_horner_kernel(
    const FieldT *coefficients, size_t coefficient_count,
    FieldT point, FieldT *partial_values)
{
    const size_t chunk = blockIdx.x * blockDim.x + threadIdx.x;
    const size_t chunks = ceil_div(coefficient_count, terms_per_thread);
    if (chunk >= chunks) return;
    const size_t begin = chunk * terms_per_thread;
    const size_t proposed_end = begin + terms_per_thread;
    const size_t end = proposed_end < coefficient_count
        ? proposed_end : coefficient_count;
    FieldT value = coefficients[end - 1];
    for (size_t i = end - 1; i > begin; --i)
        value = value * point + coefficients[i - 1];
    partial_values[chunk] = value;
}

template <typename FieldT, typename HostFT, size_t terms_per_thread = 64>
class PolynomialEvaluationContext {
    FieldT *scratch[2]{};
    TypedGpuArena arena;

public:
    explicit PolynomialEvaluationContext(size_t max_coefficients)
    {
        const size_t partials = std::max<size_t>(
            1, ceil_div(max_coefficients, terms_per_thread));
        arena.register_alloc(scratch[0], partials);
        arena.register_alloc(scratch[1], partials);
        arena.commit("DCU PolynomialEvaluationContext");
    }

    HostFT evaluate(const FieldT *coefficients, size_t count,
                    const HostFT &point)
    {
        if (count == 0) return HostFT::zero();
        const FieldT *source = coefficients;
        FieldT *destination = scratch[0];
        HostFT stage_point = point;
        unsigned stage = 0;
        constexpr size_t block = 256;
        while (count > 1) {
            const size_t partials = ceil_div(count, terms_per_thread);
            block_horner_kernel<FieldT, terms_per_thread>
                <<<ceil_div(partials, block), block>>>(
                    source, count, device_value<FieldT>(stage_point), destination);
            HIP_CHECK(hipGetLastError());
            source = destination;
            destination = scratch[(++stage) & 1U];
            count = partials;
            stage_point = stage_point ^ terms_per_thread;
        }
        HostFT result;
        HIP_CHECK(hipMemcpy(&result, source, sizeof(FieldT),
                            hipMemcpyDeviceToHost));
        return result;
    }
};

template <typename FieldT>
struct AffineTransform { FieldT scale; FieldT offset; };

template <typename FieldT>
struct AffineCompose {
    __device__ AffineTransform<FieldT> operator()(
        const AffineTransform<FieldT> &prefix,
        const AffineTransform<FieldT> &next) const
    {
        return {next.scale * prefix.scale,
                next.scale * prefix.offset + next.offset};
    }
};

template <typename FieldT>
__global__ void make_synthetic_transforms_kernel(
    const FieldT *polynomial, size_t quotient_size, FieldT point,
    AffineTransform<FieldT> *transforms)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= quotient_size) return;
    FieldT zero;
    zero.zero();
    transforms[i] = {i == 0 ? zero : point, polynomial[quotient_size - i]};
}

template <typename FieldT>
__global__ void extract_synthetic_quotient_kernel(
    const AffineTransform<FieldT> *transforms, FieldT *quotient,
    size_t quotient_size)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < quotient_size)
        quotient[quotient_size - 1 - i] = transforms[i].offset;
}

template <typename FieldT, typename HostFT>
class SyntheticDivisionContext {
    AffineTransform<FieldT> *transforms = nullptr;
    dcu::ScanContext<AffineTransform<FieldT>> scan;
    TypedGpuArena arena;

public:
    explicit SyntheticDivisionContext(size_t max_coefficients)
        : scan(std::max<size_t>(1, max_coefficients - 1))
    {
        arena.register_alloc(transforms,
                             std::max<size_t>(1, max_coefficients - 1));
        arena.commit("DCU SyntheticDivisionContext");
    }

    void divide(const FieldT *polynomial, size_t coefficient_count,
                const HostFT &point, FieldT *quotient)
    {
        if (coefficient_count < 2) return;
        const size_t quotient_size = coefficient_count - 1;
        constexpr size_t block = 256;
        make_synthetic_transforms_kernel<<<ceil_div(quotient_size, block), block>>>(
            polynomial, quotient_size, device_value<FieldT>(point), transforms);
        const AffineTransform<FieldT> identity = {
            device_value<FieldT>(HostFT::one()),
            device_value<FieldT>(HostFT::zero())};
        scan.inclusive(transforms, transforms, quotient_size,
                       AffineCompose<FieldT>{}, identity);
        extract_synthetic_quotient_kernel<<<ceil_div(quotient_size, block), block>>>(
            transforms, quotient, quotient_size);
        HIP_CHECK(hipGetLastError());
    }
};
