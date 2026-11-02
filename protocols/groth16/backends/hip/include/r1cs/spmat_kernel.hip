#pragma once

#include <cstdint>

template <typename FieldT>
__global__ void transpose_spmv_kernel(
    const uint32_t *column_offsets, const uint32_t *row_indices,
    const FieldT *values, const FieldT *input, FieldT *output, size_t columns)
{
    const size_t column = blockIdx.x * blockDim.x + threadIdx.x;
    if (column >= columns) return;
    FieldT sum;
    sum.zero();
    for (uint32_t i = column_offsets[column]; i < column_offsets[column + 1]; ++i)
        sum += values[i] * input[row_indices[i]];
    output[column] = sum;
}
