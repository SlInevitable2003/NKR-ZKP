#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "r1cs/poseidon.hpp"
#include "r1cs/r1cs.hpp"
#include "sql_query.hpp"

namespace LightZK::SqlAggregate {

template<typename S>
using Row = nkr::sql::Row<S>;

template<typename S>
using Result = nkr::sql::Result<S>;

using nkr::sql::is_power_of_two;
using nkr::sql::uint32_value;
using nkr::sql::evaluate;

template<typename S>
struct QueryVariables {
    std::vector<Variable<S>> categories;
    std::vector<Variable<S>> amounts;
    std::vector<Variable<S>> matches;
};

template<typename S>
void add_uint32_constraints(R1CSManager<S> &manager,
                            std::map<std::size_t, S> &assignment,
                            const Variable<S> &variable,
                            const S &value)
{
    const std::uint32_t raw = uint32_value(value);
    LinearCombination<S> recomposed;
    S coefficient = S::one();
    for (std::size_t bit_index = 0; bit_index < 32; ++bit_index) {
        Variable<S> bit(VariableType::Private, manager);
        const S bit_value((raw >> bit_index) & 1U);
        assignment[bit.get_id()] = bit_value;
        manager.add_constraint(bit, bit, bit);
        recomposed += LinearCombination<S>(bit) * coefficient;
        coefficient += coefficient;
    }
    manager.add_constraint(LinearCombination<S>(variable), recomposed);
}

// Implements:
//   SELECT COUNT(*), SUM(amount)
//   FROM transactions
//   WHERE category = target_category
// over a fixed-size private table committed by table_root.
template<typename S>
QueryVariables<S> add_query_constraints(
    R1CSManager<S> &manager,
    std::map<std::size_t, S> &assignment,
    const std::vector<Row<S>> &rows,
    const Variable<S> &table_root,
    const Variable<S> &target_category,
    const S &target_value,
    const Variable<S> &expected_count,
    const Variable<S> &expected_sum)
{
    if (rows.size() < 2 || !is_power_of_two(rows.size()))
        throw std::invalid_argument("SQL table row count must be a power of two and at least 2");

    QueryVariables<S> variables;
    variables.categories.reserve(rows.size());
    variables.amounts.reserve(rows.size());
    variables.matches.reserve(rows.size());
    add_uint32_constraints(manager, assignment, target_category, target_value);

    std::vector<Variable<S>> level;
    std::vector<S> level_values;
    level.reserve(rows.size());
    level_values.reserve(rows.size());

    for (const Row<S> &row : rows) {
        Variable<S> category(VariableType::Private, manager);
        Variable<S> amount(VariableType::Private, manager);
        assignment[category.get_id()] = row.category;
        assignment[amount.get_id()] = row.amount;
        variables.categories.push_back(category);
        variables.amounts.push_back(amount);
        add_uint32_constraints(manager, assignment, category, row.category);
        add_uint32_constraints(manager, assignment, amount, row.amount);

        Variable<S> leaf(VariableType::Private, manager);
        const S leaf_value = Poseidon::hash2(row.category, row.amount);
        assignment[leaf.get_id()] = leaf_value;
        Poseidon::add_hash2_constraints(manager, assignment,
                                        category, row.category,
                                        amount, row.amount,
                                        leaf);
        level.push_back(leaf);
        level_values.push_back(leaf_value);
    }

    while (level.size() > 1) {
        std::vector<Variable<S>> next_level;
        std::vector<S> next_values;
        next_level.reserve(level.size() / 2);
        next_values.reserve(level.size() / 2);
        const bool final_level = level.size() == 2;
        for (std::size_t i = 0; i < level.size(); i += 2) {
            const S digest_value = Poseidon::hash2(level_values[i], level_values[i + 1]);
            if (final_level) {
                assignment[table_root.get_id()] = digest_value;
                Poseidon::add_hash2_constraints(manager, assignment,
                                                level[i], level_values[i],
                                                level[i + 1], level_values[i + 1],
                                                table_root);
                next_level.push_back(table_root);
            } else {
                Variable<S> digest(VariableType::Private, manager);
                assignment[digest.get_id()] = digest_value;
                Poseidon::add_hash2_constraints(manager, assignment,
                                                level[i], level_values[i],
                                                level[i + 1], level_values[i + 1],
                                                digest);
                next_level.push_back(digest);
            }
            next_values.push_back(digest_value);
        }
        level = std::move(next_level);
        level_values = std::move(next_values);
    }

    LinearCombination<S> count_sum;
    LinearCombination<S> amount_sum;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        LinearCombination<S> delta(variables.categories[i]);
        delta += LinearCombination<S>(target_category) * (-S::one());

        Variable<S> match(VariableType::Private, manager);
        Variable<S> inverse(VariableType::Private, manager);
        Variable<S> selected_amount(VariableType::Private, manager);
        const bool equal = rows[i].category == target_value;
        const S match_value = equal ? S::one() : S::zero();
        const S delta_value = rows[i].category - target_value;
        const S inverse_value = equal ? S::zero() : delta_value.inverse();
        const S selected_value = match_value * rows[i].amount;
        assignment[match.get_id()] = match_value;
        assignment[inverse.get_id()] = inverse_value;
        assignment[selected_amount.get_id()] = selected_value;

        LinearCombination<S> one_minus_match(S::one());
        one_minus_match += LinearCombination<S>(match) * (-S::one());
        manager.add_constraint(one_minus_match, delta, inverse);
        manager.add_constraint(LinearCombination<S>(S::zero()), delta, match);
        manager.add_constraint(selected_amount, match, variables.amounts[i]);
        variables.matches.push_back(match);
        count_sum += match;
        amount_sum += selected_amount;
    }

    manager.add_constraint(LinearCombination<S>(expected_count), count_sum);
    manager.add_constraint(LinearCombination<S>(expected_sum), amount_sum);
    return variables;
}

} // namespace LightZK::SqlAggregate
