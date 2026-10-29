#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <omp.h>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"

namespace nkr::operators {

// Parallel variable-base MSM for Setup-only call sites.  Each worker invokes a
// single-threaded libff BDLO12 instance on a disjoint slice; no dependency-wide
// MULTICORE behavior is enabled.
template <typename Group, typename Scalar>
Group variable_base_msm(
    typename std::vector<Group>::const_iterator bases_begin,
    typename std::vector<Group>::const_iterator bases_end,
    typename std::vector<Scalar>::const_iterator scalars_begin,
    typename std::vector<Scalar>::const_iterator scalars_end)
{
    const std::size_t total = static_cast<std::size_t>(bases_end - bases_begin);
    if (total != static_cast<std::size_t>(scalars_end - scalars_begin))
        throw std::invalid_argument("MSM input size mismatch");
    if (total == 0) return Group::zero();

    const std::size_t workers = std::min(
        total, static_cast<std::size_t>(std::max(1, omp_get_max_threads())));
    const std::size_t chunk_size = (total + workers - 1) / workers;
    std::vector<Group> partial(workers, Group::zero());

    #pragma omp parallel for schedule(static)
    for (std::ptrdiff_t worker = 0;
         worker < static_cast<std::ptrdiff_t>(workers); ++worker) {
        const std::size_t begin = static_cast<std::size_t>(worker) * chunk_size;
        const std::size_t end = std::min(total, begin + chunk_size);
        if (begin == end) continue;
        partial[static_cast<std::size_t>(worker)] =
            libff::multi_exp<Group, Scalar, libff::multi_exp_method_BDLO12>(
                bases_begin + begin, bases_begin + end,
                scalars_begin + begin, scalars_begin + end, 1);
    }

    Group result = Group::zero();
    for (const Group &value : partial) result = result + value;
    return result;
}

} // namespace nkr::operators
