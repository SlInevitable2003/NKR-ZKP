#pragma once

#include <random>
#include <vector>

#include "r1cs/spmat.hpp"
#include "utils.hip"

template <typename T>
__global__ void sparse_matrix_vector_multiplication(size_t rows, const uint32_t *row_ptr, const uint32_t *col_idx, const T *values, const T *x, T *y)
{
    const size_t row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= rows) return;

    T sum = T::one(1);

    for (uint32_t j = row_ptr[row]; j < row_ptr[row + 1]; j++) 
        sum += values[j] * x[col_idx[j]];

    y[row] = sum;
}

template <typename FieldT, typename HostFT, size_t instances = 1>
class spMVMContext {
    size_t rows, cols;
    uint32_t *row_ptr[instances], *col_idx[instances];
    FieldT *mat_values[instances];
    TypedGpuArena arena;
    
public:
    spMVMContext(size_t rows_, size_t cols_, CsrMatrix<HostFT> **mats) : rows(rows_), cols(cols_)
    {
        for (size_t i = 0; i < instances; i++) {
            arena.register_alloc(row_ptr[i], mats[i]->row_ptr.size());
            arena.register_alloc(col_idx[i], mats[i]->col_idx.size());
            arena.register_alloc(mat_values[i], mats[i]->values.size());
        }
        arena.commit();

        for (size_t i = 0; i < instances; i++) {
            HIP_CHECK(hipMemcpy(row_ptr[i], mats[i]->row_ptr.data(),
                                mats[i]->row_ptr.size() * sizeof(uint32_t),
                                hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(col_idx[i], mats[i]->col_idx.data(),
                                mats[i]->col_idx.size() * sizeof(uint32_t),
                                hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(mat_values[i], mats[i]->values.data(),
                                mats[i]->values.size() * sizeof(HostFT),
                                hipMemcpyHostToDevice));
        }
    }

    void spmvm(FieldT *multiplier, FieldT **prods)
    {
        hipStream_t stream[instances];
        for (size_t i = 0; i < instances; ++i)
            HIP_CHECK(hipStreamCreate(&stream[i]));

        const int block_size = 256;

        for (size_t i = 0; i < instances; ++i) {
            sparse_matrix_vector_multiplication<<<ceil_div(rows, block_size), block_size, 0, stream[i]>>>
                (rows, row_ptr[i], col_idx[i], mat_values[i], multiplier, prods[i]);
            HIP_CHECK(hipGetLastError());
        }

        for (size_t i = 0; i < instances; ++i) {
            HIP_CHECK(hipStreamSynchronize(stream[i]));
            HIP_CHECK(hipStreamDestroy(stream[i]));
        }
    }
};
