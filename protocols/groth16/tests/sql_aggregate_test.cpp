#include <cassert>
#include <cstddef>
#include <map>
#include <vector>

#include "groth16/protocol.hpp"
#include "groth16/types.hpp"
#include "r1cs/r1cs.hpp"
#include "r1cs/sql_aggregate.hpp"

using FieldT = nkr::groth16::Scalar;
using namespace LightZK;

int main()
{
    nkr::groth16::Curve::init_public_params();

    const std::vector<SqlAggregate::Row<FieldT>> rows{
        {FieldT(1), FieldT(10)},
        {FieldT(2), FieldT(20)},
        {FieldT(1), FieldT(30)},
        {FieldT(3), FieldT(40)},
    };
    const FieldT target(1);
    const auto result = SqlAggregate::evaluate(rows, target);
    assert(result.count == FieldT(2));
    assert(result.sum == FieldT(40));

    R1CSManager<FieldT> manager;
    std::map<std::size_t, FieldT> assignment;
    Variable<FieldT> table_root(VariableType::Public, manager);
    Variable<FieldT> target_category(VariableType::Public, manager);
    Variable<FieldT> expected_count(VariableType::Public, manager);
    Variable<FieldT> expected_sum(VariableType::Public, manager);
    assignment[table_root.get_id()] = result.table_root;
    assignment[target_category.get_id()] = target;
    assignment[expected_count.get_id()] = result.count;
    assignment[expected_sum.get_id()] = result.sum;
    const auto variables = SqlAggregate::add_query_constraints(
        manager, assignment, rows, table_root, target_category, target,
        expected_count, expected_sum);

    // For N rows: 439 * (2N - 1) Poseidon constraints, 66N row
    // range constraints, 33 target range constraints, 3N filter
    // constraints, and two aggregate equalities = 947N - 404.
    assert(manager.num_constraints() == 3384);
    assert(manager.num_variables() == 3385); // includes constant-one
    assert(manager.num_public() == 4);

    nkr::groth16::Circuit circuit;
    manager.gen_spmat(circuit.a, circuit.b, circuit.c, true);
    circuit.public_inputs = manager.num_public();
    assert(circuit.constraints() == 4096);
    assert(circuit.a.num_cols == 4096);

    std::vector<FieldT> witness(circuit.a.num_cols, FieldT::zero());
    witness[0] = FieldT::one();
    for (const auto &[id, value] : assignment)
        witness[manager.dense_index(id)] = value;
    assert(nkr::groth16::check_witness(circuit, witness));

    auto tampered_sum = witness;
    tampered_sum[manager.dense_index(expected_sum.get_id())] += FieldT::one();
    assert(!nkr::groth16::check_witness(circuit, tampered_sum));

    auto tampered_root = witness;
    tampered_root[manager.dense_index(table_root.get_id())] += FieldT::one();
    assert(!nkr::groth16::check_witness(circuit, tampered_root));

    auto tampered_row = witness;
    tampered_row[manager.dense_index(variables.amounts.front().get_id())] += FieldT::one();
    assert(!nkr::groth16::check_witness(circuit, tampered_row));

    auto tampered_target = witness;
    tampered_target[manager.dense_index(target_category.get_id())] += FieldT::one();
    assert(!nkr::groth16::check_witness(circuit, tampered_target));
    return 0;
}
