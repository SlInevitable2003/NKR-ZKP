#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "plonkish/arithmetization.hpp"
#include "plonkish/poseidon.hpp"
#include "sql_query.hpp"

namespace Plonkish::SqlAggregate {

using Scalar = nkr::plonk::Scalar;

// Constrains a UINT32 variable by decomposing it into 32 private bits and
// recomposing the value.
inline void add_uint32_constraints(PlonkishManager &manager,
                                   Variable variable,
                                   const Scalar &value)
{
    const std::uint32_t raw = nkr::sql::uint32_value(value);
    std::vector<LinearTerm> terms;
    terms.reserve(32);
    for (std::size_t bit_index = 0; bit_index < 32; ++bit_index) {
        const Scalar bit_value((raw >> bit_index) & 1U);
        const Variable bit = manager.private_variable(bit_value);
        const Variable squared = manager.multiply(bit, bit);
        manager.assert_equal(bit, squared);
        terms.push_back(
            {Scalar(static_cast<long>(std::uint64_t{1} << bit_index)), bit});
    }
    const Variable recomposed = manager.linear_combination(terms);
    manager.assert_equal(variable, recomposed);
}

struct QueryVariables {
    std::vector<Variable> categories;
    std::vector<Variable> amounts;
    std::vector<Variable> matches;
};

// Implements:
//   SELECT COUNT(*), SUM(amount)
//   FROM transactions
//   WHERE category = target_category
// over a fixed-size private table committed by table_root. The four public
// variables (root, target, count, sum) are allocated by the caller.
inline QueryVariables add_query_constraints(
    PlonkishManager &manager,
    const std::vector<nkr::sql::Row<Scalar>> &rows,
    Variable table_root,
    Variable target_category,
    const Scalar &target_value,
    Variable expected_count,
    Variable expected_sum)
{
    if (rows.size() < 2 || !nkr::sql::is_power_of_two(rows.size()))
        throw std::invalid_argument(
            "SQL table row count must be a power of two and at least 2");

    QueryVariables variables;
    variables.categories.reserve(rows.size());
    variables.amounts.reserve(rows.size());
    variables.matches.reserve(rows.size());
    add_uint32_constraints(manager, target_category, target_value);

    const Variable one = manager.constant(Scalar::one());
    const Variable zero = manager.constant(Scalar::zero());

    std::vector<Variable> level;
    level.reserve(rows.size());
    for (const nkr::sql::Row<Scalar> &row : rows) {
        const Variable category = manager.private_variable(row.category);
        const Variable amount = manager.private_variable(row.amount);
        add_uint32_constraints(manager, category, row.category);
        add_uint32_constraints(manager, amount, row.amount);
        const Variable leaf = Poseidon::add_hash2(
            manager, category, amount, VariableVisibility::Private);
        level.push_back(leaf);
        variables.categories.push_back(category);
        variables.amounts.push_back(amount);
    }

    while (level.size() > 1) {
        std::vector<Variable> next;
        next.reserve(level.size() / 2);
        for (std::size_t i = 0; i < level.size(); i += 2) {
            next.push_back(Poseidon::add_hash2(
                manager, level[i], level[i + 1], VariableVisibility::Private));
        }
        level = std::move(next);
    }
    manager.assert_equal(table_root, level.front());

    std::vector<LinearTerm> count_terms;
    std::vector<LinearTerm> sum_terms;
    count_terms.reserve(rows.size());
    sum_terms.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Scalar delta_value =
            manager.value(variables.categories[i]) - manager.value(target_category);
        const bool equal = delta_value == Scalar::zero();
        const Scalar match_value = equal ? Scalar::one() : Scalar::zero();
        const Scalar inverse_value = equal ? Scalar::zero() : delta_value.inverse();
        const Scalar selected_value =
            match_value * manager.value(variables.amounts[i]);

        const Variable match = manager.private_variable(match_value);
        const Variable inverse = manager.private_variable(inverse_value);
        const Variable selected = manager.private_variable(selected_value);
        const Variable delta = manager.subtract(variables.categories[i], target_category);
        const Variable one_minus_match = manager.subtract(one, match);

        manager.enforce_multiplication(one_minus_match, delta, inverse);
        manager.enforce_multiplication(zero, delta, match);
        manager.enforce_multiplication(selected, match, variables.amounts[i]);

        variables.matches.push_back(match);
        count_terms.push_back({Scalar::one(), match});
        sum_terms.push_back({Scalar::one(), selected});
    }

    const Variable count = manager.linear_combination(count_terms);
    const Variable sum = manager.linear_combination(sum_terms);
    manager.assert_equal(expected_count, count);
    manager.assert_equal(expected_sum, sum);
    return variables;
}

} // namespace Plonkish::SqlAggregate
