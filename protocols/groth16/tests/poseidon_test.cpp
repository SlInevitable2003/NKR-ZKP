#include <cassert>
#include <cstddef>
#include <map>
#include <vector>

#include "groth16/protocol.hpp"
#include "groth16/types.hpp"
#include "r1cs/poseidon.hpp"
#include "r1cs/r1cs.hpp"

using FieldT = nkr::groth16::Scalar;
using namespace LightZK;

int main()
{
    nkr::groth16::Curve::init_public_params();

    const FieldT left(1);
    const FieldT right(2);
    const FieldT digest = Poseidon::hash2(left, right);
    const libff::bigint<FieldT::num_limbs> expected(
        "7853200120776062878684798364095072458815029376092732009249414926327459813530");
    assert(digest.as_bigint() == expected);

    R1CSManager<FieldT> manager;
    std::map<std::size_t, FieldT> assignment;
    Variable<FieldT> digest_variable(VariableType::Public, manager);
    Variable<FieldT> left_variable(VariableType::Private, manager);
    Variable<FieldT> right_variable(VariableType::Private, manager);
    assignment[digest_variable.get_id()] = digest;
    assignment[left_variable.get_id()] = left;
    assignment[right_variable.get_id()] = right;
    Poseidon::add_hash2_constraints(manager, assignment,
                                    left_variable, left,
                                    right_variable, right,
                                    digest_variable);

    // 81 S-boxes x 3 multiplication constraints, 65 MDS layers x 3
    // linear constraints, and one digest equality.
    assert(manager.num_constraints() == 439);
    assert(manager.num_variables() == 442); // includes the constant-one column
    assert(manager.num_public() == 1);

    nkr::groth16::Circuit circuit;
    manager.gen_spmat(circuit.a, circuit.b, circuit.c, true);
    circuit.public_inputs = manager.num_public();
    assert(circuit.constraints() == 512);
    assert(circuit.a.num_cols == 512);

    std::vector<FieldT> witness(circuit.a.num_cols, FieldT::zero());
    witness[0] = FieldT::one();
    for (const auto &[id, value] : assignment)
        witness[manager.dense_index(id)] = value;
    assert(nkr::groth16::check_witness(circuit, witness));

    witness[manager.dense_index(digest_variable.get_id())] += FieldT::one();
    assert(!nkr::groth16::check_witness(circuit, witness));
    return 0;
}
