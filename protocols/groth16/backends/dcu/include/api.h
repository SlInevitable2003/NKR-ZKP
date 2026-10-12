#include "hip/hip_runtime.h"
#pragma once
#include <cstdio>

#include "fields/alt_bn128-fp2.hip"
#include "curves/jacobian_t.hip"
#include "curves/xyzz_t.hip"
// #include "msm/msm_kernel.hip"
// #include "ntt/ntt_common.hip"

#include "msm/msm.hip"
#include "ntt/ntt.hip"
#include "spmvm/spmvm.hip"


namespace alt_bn128 {
    typedef jacobian_t<fp_t> g1_t;
    typedef jacobian_t<fp2_t> g2_t;
    typedef xyzz_t<fp_t> g1_bucket_t;
    typedef xyzz_t<fp2_t> g2_bucket_t;
}
/*
#define CUDA_CHECK(ans) { hipAssert((ans), __FILE__, __LINE__); }
inline void hipAssert(hipError_t code, const char *file, int line) {
   if (code != hipSuccess) {
      fprintf(stderr, "HIP Error: %s %s %d\n", hipGetErrorString(code), file, line);
      exit(code);
   }
}

template<typename T>
__device__ __host__ void print_mem(const T& val) {
   const uint8_t *ptr = reinterpret_cast<const uint8_t*>(&val);
   for (int i = 0; i < sizeof(T); i++) printf("%02x", ptr[i]);
   printf("\n");
}

template<typename F, typename... Args> __global__ void kernel(F func, Args... args) { func(args...); }
*/