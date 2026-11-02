#pragma once

#include <cassert>

#include "fields/alt_bn128-fp2.hip"
#include "polynomial/polynomial.hip"
#include "utils.hip"

template <typename FieldT>
__global__ void qap_power_table_kernel(FieldT *table, size_t count)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    table[0] = FieldT::one();
    const FieldT factor = table[1];
    for (size_t i = 2; i < count; ++i) table[i] = table[i - 1] * factor;
}

template <typename FieldT>
__global__ void lagrange_polynomial_kernel(
    const FieldT *omega_powers, const FieldT *parameters,
    FieldT *lagrange, size_t count)
{
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const FieldT denominator_inverse =
        dcu_field_inverse(parameters[0] - omega_powers[i]);
    lagrange[i] = parameters[1] * omega_powers[i] * denominator_inverse;
}

template <typename FieldT, typename HostFT>
class QAPContext {
    size_t count;
    FieldT *omega_powers = nullptr;
    FieldT *lagrange_values = nullptr;
    FieldT *parameters = nullptr;
    TypedGpuArena arena;

public:
    QAPContext(size_t count_, HostFT omega) : count(count_)
    {
        assert(count >= 2 && (count & (count - 1)) == 0 &&
               "QAP domain size must be a power of two and at least two");
        arena.register_alloc(omega_powers, count);
        arena.register_alloc(lagrange_values, count);
        arena.register_alloc(parameters, 2);
        arena.commit("DCU QAPContext");
        HIP_CHECK(hipMemcpy(omega_powers + 1, &omega, sizeof(FieldT),
                            hipMemcpyHostToDevice));
        qap_power_table_kernel<<<1, 1>>>(omega_powers, count);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());
    }

    void prepare(HostFT t, HostFT z_t)
    {
        const HostFT host[2] = {t, z_t * HostFT(count).inverse()};
        HIP_CHECK(hipMemcpy(parameters, host, sizeof(host),
                            hipMemcpyHostToDevice));
    }

    void compute_lagrange()
    {
        constexpr size_t block = 256;
        lagrange_polynomial_kernel<<<ceil_div(count, block), block>>>(
            omega_powers, parameters, lagrange_values, count);
        HIP_CHECK(hipDeviceSynchronize());
        HIP_CHECK(hipGetLastError());
    }

    FieldT *lagrange() { return lagrange_values; }
};
