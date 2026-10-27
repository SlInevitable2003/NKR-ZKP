#pragma once

#include <hip/hip_runtime.h>

#include "fields/alt_bn128-fp2.hip"
#include "curves/jacobian_t.hip"
#include "curves/xyzz_t.hip"
#include "msm/msm.hip"
#include "ntt/ntt.hip"
#include "utils.hip"

namespace alt_bn128 {
using g1_t = jacobian_t<fp_t>;
using g2_t = jacobian_t<fp2_t>;
using g1_bucket_t = xyzz_t<fp_t>;
using g2_bucket_t = xyzz_t<fp2_t>;
} // namespace alt_bn128
