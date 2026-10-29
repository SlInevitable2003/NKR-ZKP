#pragma once

#include "fields/alt_bn128-fp2.cuh"
#include "curves/jacobian_t.cuh"
#include "curves/xyzz_t.cuh"
#include "msm/fixmsm_kernel.cuh"
#include "msm/large_msm.cuh"
#include "msm/msm.cuh"
#include "ntt/ntt.cuh"
#include "polynomial/polynomial.cuh"
#include "utils/arith.cuh"
#include "utils/arena.cuh"
#include "utils/binary_archive.cuh"
#include "utils/cuda_check.cuh"
#include "utils/gpu_config.cuh"
#include "utils/kernel.cuh"

namespace alt_bn128 {
using g1_t = jacobian_t<fp_t>;
using g2_t = jacobian_t<fp2_t>;
using g1_bucket_t = xyzz_t<fp_t>;
using g2_bucket_t = xyzz_t<fp2_t>;
} // namespace alt_bn128
