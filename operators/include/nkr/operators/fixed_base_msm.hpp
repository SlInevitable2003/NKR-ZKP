#pragma once

#include <cstddef>
#include <vector>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"

namespace nkr::operators {

// Computes a batch of independent scalar multiplications which all share one
// base.  The libff window table is built once and the batch is distributed at
// this call site, so this remains parallel even when libff's global MULTICORE
// option is disabled.
template <typename Group, typename Scalar>
class FixedBaseMsm {
public:
    FixedBaseMsm(const Group &base, const std::size_t scalar_count)
        : scalar_size_(Scalar::size_in_bits()),
          window_(libff::get_exp_window_size<Group>(scalar_count)),
          table_(libff::get_window_table(scalar_size_, window_, base))
    {}

    std::vector<Group> operator()(const std::vector<Scalar> &scalars) const
    {
        std::vector<Group> result(scalars.size());
        #pragma omp parallel for schedule(static)
        for (std::ptrdiff_t i = 0;
             i < static_cast<std::ptrdiff_t>(scalars.size()); ++i) {
            result[static_cast<std::size_t>(i)] = libff::windowed_exp(
                scalar_size_, window_, table_, scalars[static_cast<std::size_t>(i)]);
        }
        return result;
    }

private:
    std::size_t scalar_size_;
    std::size_t window_;
    libff::window_table<Group> table_;
};

} // namespace nkr::operators
