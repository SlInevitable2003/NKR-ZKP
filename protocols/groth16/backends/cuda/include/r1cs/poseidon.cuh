#pragma once
#include "fields/alt_bn128.cuh"

#if defined(__GNUC__) || defined(__clang__)
# define FR_T(vec_t, ...) fr_t{(vec_t){__VA_ARGS__}}
#else
# define FR_T(vec_t, ...) fr_t{vec_t{__VA_ARGS__}}
#endif

namespace alt_bn128 {
    typedef uint64_t vec256[4];

    
}

#undef FR_T