#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "crypto/poseidon.hpp"

namespace nkr::sql {

/*
    Shared semantics of the fixed SQL example:

        SELECT COUNT(*), SUM(amount)
        FROM transactions
        WHERE category = target_category;

    Rows are private inputs committed by a Poseidon Merkle root. The category
    and amount columns are UINT32 values; the root, target category, COUNT and
    SUM are public. The protocol-specific constraint builders live with each
    protocol; this header only provides the reference evaluation used to build
    assignments and check results.
*/
template<typename S>
struct Row {
    S category;
    S amount;
};

template<typename S>
struct Result {
    S table_root = S::zero();
    S count = S::zero();
    S sum = S::zero();
};

inline bool is_power_of_two(const std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

template<typename S>
std::uint32_t uint32_value(const S &value)
{
    const unsigned long raw = value.as_ulong();
    if (raw > UINT32_MAX || S(static_cast<long>(raw)) != value)
        throw std::invalid_argument("SQL UINT32 value is outside its canonical range");
    return static_cast<std::uint32_t>(raw);
}

template<typename S>
Result<S> evaluate(const std::vector<Row<S>> &rows, const S &target_category)
{
    if (rows.size() < 2 || !is_power_of_two(rows.size()))
        throw std::invalid_argument("SQL table row count must be a power of two and at least 2");

    uint32_value(target_category);
    std::vector<S> hashes;
    hashes.reserve(rows.size());
    Result<S> result;
    for (const Row<S> &row : rows) {
        uint32_value(row.category);
        uint32_value(row.amount);
        hashes.push_back(nkr::poseidon::hash2<S>(row.category, row.amount));
        if (row.category == target_category) {
            result.count += S::one();
            result.sum += row.amount;
        }
    }

    while (hashes.size() > 1) {
        std::vector<S> next;
        next.reserve(hashes.size() / 2);
        for (std::size_t i = 0; i < hashes.size(); i += 2)
            next.push_back(nkr::poseidon::hash2<S>(hashes[i], hashes[i + 1]));
        hashes = std::move(next);
    }
    result.table_root = hashes.front();
    return result;
}

} // namespace nkr::sql
