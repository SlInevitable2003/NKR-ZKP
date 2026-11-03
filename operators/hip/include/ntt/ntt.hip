#pragma once

#include <algorithm>
#include <cassert>

#include "fields/alt_bn128-fp2.hip"
#include "ntt/ntt_kernel.hip"
#include "polynomial/polynomial.hip"
#include "utils.hip"

template <typename FieldT>
__global__ void power_computing(FieldT *values, size_t count)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    values[0] = FieldT::one();
    const FieldT factor = values[1];
    for (size_t i = 2; i < count; ++i) values[i] = values[i - 1] * factor;
}

template <typename FieldT>
__global__ void quotient_relation_kernel(
    FieldT *a, const FieldT *b, const FieldT *c,
    const FieldT *coset_powers, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const FieldT denominator = coset_powers[count] - FieldT::one();
    a[i] = (a[i] * b[i] - c[i]) * dcu_field_inverse(denominator);
}

template <typename FieldT, typename HostFT>
class NTTContext {
    size_t scale;
    FieldT *scale_inverse = nullptr;
    FieldT *omega_power_table = nullptr;
    FieldT *coset_power_table = nullptr;
    FieldT *inverse_omega_power_table = nullptr;
    FieldT *inverse_coset_power_table = nullptr;
    FieldT *buffer = nullptr;
    TypedGpuArena arena;

    void allocate()
    {
        arena.register_alloc(scale_inverse, 1);
        arena.register_alloc(omega_power_table, scale);
        arena.register_alloc(coset_power_table, scale + 1);
        arena.register_alloc(inverse_omega_power_table, scale);
        arena.register_alloc(inverse_coset_power_table, scale);
        arena.register_alloc(buffer, scale);
        arena.commit("DCU NTTContext");
    }

    void initialize_scale_inverse()
    {
        HostFT inverse = HostFT(static_cast<long>(scale)).inverse();
        HIP_CHECK(hipMemcpy(scale_inverse, &inverse, sizeof(FieldT),
                            hipMemcpyHostToDevice));
    }

public:
    NTTContext(size_t scale_, HostFT omega, HostFT coset) : scale(scale_)
    {
        assert((scale & (scale - 1)) == 0 && scale >= 2);
        static_assert(sizeof(HostFT) == sizeof(FieldT));
        allocate();
        initialize_scale_inverse();
        HIP_CHECK(hipMemcpy(omega_power_table + 1, &omega, sizeof(FieldT),
                            hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(coset_power_table + 1, &coset, sizeof(FieldT),
                            hipMemcpyHostToDevice));
        omega.invert();
        coset.invert();
        HIP_CHECK(hipMemcpy(inverse_omega_power_table + 1, &omega,
                            sizeof(FieldT), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(inverse_coset_power_table + 1, &coset,
                            sizeof(FieldT), hipMemcpyHostToDevice));
        power_computing<<<1, 1>>>(omega_power_table, scale);
        power_computing<<<1, 1>>>(coset_power_table, scale + 1);
        power_computing<<<1, 1>>>(inverse_omega_power_table, scale);
        power_computing<<<1, 1>>>(inverse_coset_power_table, scale);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());
    }

    NTTContext(size_t scale_, const HostFT *omega_powers,
               const HostFT *coset_powers, const HostFT *inverse_omega_powers,
               const HostFT *inverse_coset_powers)
        : scale(scale_)
    {
        assert((scale & (scale - 1)) == 0 && scale >= 2);
        static_assert(sizeof(HostFT) == sizeof(FieldT));
        allocate();
        initialize_scale_inverse();
        HIP_CHECK(hipMemcpy(omega_power_table, omega_powers,
                            scale * sizeof(FieldT), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(coset_power_table, coset_powers,
                            (scale + 1) * sizeof(FieldT), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(inverse_omega_power_table, inverse_omega_powers,
                            scale * sizeof(FieldT), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(inverse_coset_power_table, inverse_coset_powers,
                            scale * sizeof(FieldT), hipMemcpyHostToDevice));
    }

    const FieldT *omega_powers() const { return omega_power_table; }

    void ntt(FieldT *polynomial, bool inverse = false)
    {
        const uint32_t rounds = static_cast<uint32_t>(log2_floor(scale));
        FieldT *omegas = inverse ? inverse_omega_power_table : omega_power_table;

        if (scale < (size_t{1} << 20)) {
            constexpr size_t block = 256;
            bit_reverse_copy<<<ceil_div(scale, block), block>>>(
                polynomial, buffer, rounds, scale);
            FieldT *source = buffer;
            FieldT *destination = polynomial;
            for (size_t span = 2; span <= scale; span <<= 1) {
                radix2_ntt_stage<<<ceil_div(scale / 2, block), block>>>(
                    source, destination, omegas, scale, span);
                FieldT *temporary = source;
                source = destination;
                destination = temporary;
            }
            if (source != polynomial)
                HIP_CHECK(hipMemcpy(polynomial, source, scale * sizeof(FieldT),
                                    hipMemcpyDeviceToDevice));
            if (inverse)
                multiply_constant_kernel<<<ceil_div(scale, block), block>>>(
                    polynomial, scale_inverse, scale);
            HIP_CHECK(hipDeviceSynchronize());
            HIP_CHECK(hipGetLastError());
            return;
        }

        const size_t final_batch_size =
            scale / (DCU_NTT_BLOCK_SIZE * DCU_NTT_BLOCK_SIZE);
        const size_t final_block_size =
            std::max(final_batch_size, size_t{32});
        assert(final_block_size <= 1024 &&
               "on-chip NTT requires at most 1024 threads per block");

        dcu_onchip_butterfly_transformation_1
            <<<scale / DCU_NTT_BLOCK_SIZE, DCU_NTT_BLOCK_SIZE>>>(
                polynomial, buffer, omegas, scale);
        dcu_onchip_butterfly_transformation_2
            <<<scale / DCU_NTT_BLOCK_SIZE, DCU_NTT_BLOCK_SIZE>>>(
                buffer, polynomial, omegas, scale);
        dcu_onchip_butterfly_transformation_3
            <<<scale / final_block_size, final_block_size,
               final_block_size * sizeof(FieldT)>>>(
                polynomial, buffer, omegas, scale, final_batch_size);
        constexpr size_t final_kernel_block = 256;
        bit_reverse_copy
            <<<ceil_div(scale, final_kernel_block), final_kernel_block>>>(
                buffer, polynomial, rounds, scale);
        if (inverse)
            multiply_constant_kernel
                <<<ceil_div(scale, final_kernel_block), final_kernel_block>>>(
                    polynomial, scale_inverse, scale);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());
    }

    void intt(FieldT *polynomial) { ntt(polynomial, true); }
    void coset_ntt(FieldT *polynomial)
    {
        constexpr size_t block = 256;
        multiply_table_kernel<<<ceil_div(scale, block), block>>>(
            polynomial, coset_power_table, scale);
        ntt(polynomial);
    }
    void coset_intt(FieldT *polynomial)
    {
        intt(polynomial);
        constexpr size_t block = 256;
        multiply_table_kernel<<<ceil_div(scale, block), block>>>(
            polynomial, inverse_coset_power_table, scale);
    }

    void A_times_B_minus_C_divided_by_Z(
        FieldT *a, FieldT *b, FieldT *c)
    {
        constexpr size_t block = 256;
        quotient_relation_kernel<<<ceil_div(scale, block), block>>>(
            a, b, c, coset_power_table, scale);
        HIP_CHECK(hipGetLastError());
    }
};
