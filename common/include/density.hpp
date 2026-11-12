#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace nkr {

/*
    Input density levels shared by the matrix-multiplication circuits:

    - Full   (FULL)  : 100% random nonzero entries;
    - Hybrid (HYBRID): 50% random nonzero entries, the rest are zero;
    - Sparse (SPARSE): 10% random nonzero entries, the rest are zero.

    The nonzero counts are ceil(N * density), so small matrices keep at least
    one nonzero entry. Zero positions are shuffled and A/B are sampled
    independently.
*/
enum class Density { Full, Hybrid, Sparse };

namespace density_detail {

inline std::string ascii_lowercase(std::string text)
{
    for (char &character : text)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

}

inline Density parse_density(const std::string &name)
{
    const std::string normalized = density_detail::ascii_lowercase(name);
    if (normalized == "full") return Density::Full;
    if (normalized == "hybrid") return Density::Hybrid;
    if (normalized == "sparse") return Density::Sparse;
    throw std::invalid_argument("unknown density: " + name +
                                " (expected full, hybrid, or sparse)");
}

inline const char *density_name(const Density density)
{
    switch (density) {
    case Density::Full: return "full";
    case Density::Hybrid: return "hybrid";
    case Density::Sparse: return "sparse";
    }
    throw std::invalid_argument("invalid density");
}

inline std::size_t density_nonzero_count(const std::size_t count, const Density density)
{
    switch (density) {
    case Density::Full: return count;
    case Density::Hybrid: return count - count / 2;
    case Density::Sparse: return count / 10 + (count % 10 != 0);
    }
    throw std::invalid_argument("invalid density");
}

/*
    Random zero mask for one input vector. `sample` returns zero at masked
    positions and retries the generator until it produces a nonzero value
    elsewhere, so the nonzero ratio is exact up to the ceil rounding.
*/
class DensitySampler {
public:
    DensitySampler(const std::size_t count, const Density density)
        : mask_(count, 1), nonzero_(density_nonzero_count(count, density))
    {
        const std::size_t zeros = count - nonzero_;
        if (zeros == 0) return;
        std::fill(mask_.end() - zeros, mask_.end(), 0);
        std::mt19937_64 engine(std::random_device{}());
        std::shuffle(mask_.begin(), mask_.end(), engine);
    }

    std::size_t size() const { return mask_.size(); }
    std::size_t nonzero_count() const { return nonzero_; }
    std::size_t zero_count() const { return mask_.size() - nonzero_; }
    bool is_zero(const std::size_t index) const { return mask_.at(index) == 0; }

    template<typename Field, typename Generator>
    Field sample(const std::size_t index, Generator generate) const
    {
        if (is_zero(index)) return Field::zero();
        Field value;
        do {
            value = generate();
        } while (value == Field::zero());
        return value;
    }

private:
    std::vector<unsigned char> mask_;
    std::size_t nonzero_;
};

}
