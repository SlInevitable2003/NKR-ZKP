#pragma once

#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace nkr {

enum class DataMode { Dense, Sparse, Hybrid };

inline DataMode parse_data_mode(const std::string &name)
{
    if (name == "dense") return DataMode::Dense;
    if (name == "sparse") return DataMode::Sparse;
    if (name == "hybrid") return DataMode::Hybrid;
    throw std::invalid_argument("unknown data mode: " + name +
                                " (expected dense, sparse, or hybrid)");
}

inline const char *data_mode_name(const DataMode mode)
{
    switch (mode) {
    case DataMode::Dense: return "dense";
    case DataMode::Sparse: return "sparse";
    case DataMode::Hybrid: return "hybrid";
    }
    throw std::invalid_argument("invalid data mode");
}

class InputSampler {
public:
    InputSampler(const std::size_t count, const DataMode mode)
        : random_mask_(count, 1)
    {
        if (mode == DataMode::Dense) return;
        const std::size_t nonzero_count = mode == DataMode::Hybrid ?
            count / 10 + (count % 10 != 0) : count - count / 2;
        const std::size_t zero_count = count - nonzero_count;
        if (zero_count == 0) return;
        std::fill(random_mask_.end() - zero_count, random_mask_.end(), 0);
        std::mt19937_64 engine(std::random_device{}());
        std::shuffle(random_mask_.begin(), random_mask_.end(), engine);
    }

    template<typename Field, typename Generator>
    Field sample(const std::size_t index, Generator generate) const
    {
        if (random_mask_.at(index) == 0) return Field::zero();
        Field value;
        do {
            value = generate();
        } while (value == Field::zero());
        return value;
    }

private:
    std::vector<unsigned char> random_mask_;
};

}
