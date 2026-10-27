#include "plonk/arithmetization.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace nkr::plonk {
namespace {

std::size_t next_power_of_two(std::size_t value)
{
    if (value <= 1) return 1;
    std::size_t result = 1;
    while (result < value) {
        if (result > std::numeric_limits<std::size_t>::max() / 2)
            throw std::overflow_error("PLONK domain size overflow");
        result <<= 1;
    }
    return result;
}

} // namespace

Variable PlonkishManager::allocate(VariableVisibility visibility, const Scalar &value)
{
    if (variables_.size() >= Variable::invalid_index)
        throw std::overflow_error("PLONK variable index overflow");
    const Variable variable(static_cast<Variable::Index>(variables_.size()));
    variables_.push_back({visibility, value});
    if (visibility == VariableVisibility::Public) public_variables_.push_back(variable);
    return variable;
}

Variable PlonkishManager::public_variable(const Scalar &value)
{
    return allocate(VariableVisibility::Public, value);
}

Variable PlonkishManager::private_variable(const Scalar &value)
{
    return allocate(VariableVisibility::Private, value);
}

void PlonkishManager::validate_variable(Variable variable) const
{
    if (!variable.valid() || variable.index() >= variables_.size())
        throw std::out_of_range("invalid PLONK variable");
}

const Scalar &PlonkishManager::value(Variable variable) const
{
    validate_variable(variable);
    return variables_[variable.index()].value;
}

void PlonkishManager::add_arithmetic_gate(Variable a,
                                           Variable b,
                                           Variable c,
                                           const Scalar &q_l,
                                           const Scalar &q_r,
                                           const Scalar &q_m,
                                           const Scalar &q_o,
                                           const Scalar &q_c)
{
    if (a.valid()) validate_variable(a);
    if (b.valid()) validate_variable(b);
    if (c.valid()) validate_variable(c);
    gates_.push_back({a, b, c, q_l, q_r, q_m, q_o, q_c});
}

Variable PlonkishManager::constant(const Scalar &constant_value)
{
    const Variable output = private_variable(constant_value);
    add_arithmetic_gate(output, {}, {}, Scalar::one(), Scalar::zero(), Scalar::zero(),
                        Scalar::zero(), -constant_value);
    return output;
}

Variable PlonkishManager::add(Variable lhs, Variable rhs)
{
    const Variable output = private_variable(value(lhs) + value(rhs));
    add_arithmetic_gate(lhs, rhs, output, Scalar::one(), Scalar::one(), Scalar::zero(),
                        -Scalar::one(), Scalar::zero());
    return output;
}

Variable PlonkishManager::subtract(Variable lhs, Variable rhs)
{
    const Variable output = private_variable(value(lhs) - value(rhs));
    add_arithmetic_gate(lhs, rhs, output, Scalar::one(), -Scalar::one(), Scalar::zero(),
                        -Scalar::one(), Scalar::zero());
    return output;
}

Variable PlonkishManager::multiply(Variable lhs, Variable rhs)
{
    const Variable output = private_variable(value(lhs) * value(rhs));
    enforce_multiplication(output, lhs, rhs);
    return output;
}

Variable PlonkishManager::linear_combination(const std::vector<LinearTerm> &terms,
                                               const Scalar &constant_term)
{
    if (terms.empty()) return constant(constant_term);

    for (const auto &term : terms) validate_variable(term.variable);

    Scalar accumulator_value = constant_term + terms.front().coefficient * value(terms.front().variable);
    Variable accumulator = private_variable(accumulator_value);
    add_arithmetic_gate(terms.front().variable, {}, accumulator,
                        terms.front().coefficient, Scalar::zero(), Scalar::zero(),
                        -Scalar::one(), constant_term);

    for (std::size_t i = 1; i < terms.size(); ++i) {
        accumulator_value += terms[i].coefficient * value(terms[i].variable);
        const Variable next = private_variable(accumulator_value);
        add_arithmetic_gate(accumulator, terms[i].variable, next,
                            Scalar::one(), terms[i].coefficient, Scalar::zero(),
                            -Scalar::one(), Scalar::zero());
        accumulator = next;
    }
    return accumulator;
}

void PlonkishManager::assert_equal(Variable lhs, Variable rhs)
{
    validate_variable(lhs);
    validate_variable(rhs);
    add_arithmetic_gate(lhs, rhs, {}, Scalar::one(), -Scalar::one(), Scalar::zero(),
                        Scalar::zero(), Scalar::zero());
}

void PlonkishManager::enforce_multiplication(Variable output, Variable lhs, Variable rhs)
{
    validate_variable(output);
    validate_variable(lhs);
    validate_variable(rhs);
    add_arithmetic_gate(lhs, rhs, output, Scalar::zero(), Scalar::zero(), Scalar::one(),
                        -Scalar::one(), Scalar::zero());
}

Arithmetization PlonkishManager::finalize(std::size_t minimum_domain_size) const
{
    const std::size_t used_rows = public_variables_.size() + gates_.size();
    const std::size_t domain_size = next_power_of_two(std::max(used_rows, minimum_domain_size));
    if (domain_size > std::numeric_limits<std::uint32_t>::max() / 3)
        throw std::overflow_error("PLONK copy-permutation index overflow");

    Arithmetization result;
    Circuit &circuit = result.circuit;
    Witness &witness = result.witness;
    circuit.domain_size = domain_size;
    circuit.rows = used_rows;
    circuit.public_inputs = public_variables_.size();
    circuit.q_l.assign(domain_size, Scalar::zero());
    circuit.q_r.assign(domain_size, Scalar::zero());
    circuit.q_m.assign(domain_size, Scalar::zero());
    circuit.q_o.assign(domain_size, Scalar::zero());
    circuit.q_c.assign(domain_size, Scalar::zero());
    circuit.sigma_1.resize(domain_size);
    circuit.sigma_2.resize(domain_size);
    circuit.sigma_3.resize(domain_size);

    witness.a.assign(domain_size, Scalar::zero());
    witness.b.assign(domain_size, Scalar::zero());
    witness.c.assign(domain_size, Scalar::zero());
    witness.public_input.assign(domain_size, Scalar::zero());

    std::array<std::vector<Variable>, 3> wires;
    for (auto &wire : wires) wire.assign(domain_size, Variable{});

    // Public values occupy the first rows. The convention -A + PI = 0 keeps
    // the externally visible PI values positive.
    for (std::size_t row = 0; row < public_variables_.size(); ++row) {
        const Variable variable = public_variables_[row];
        wires[0][row] = variable;
        witness.a[row] = value(variable);
        witness.public_input[row] = value(variable);
        circuit.q_l[row] = -Scalar::one();
    }

    const auto wire_value = [&](Variable variable) {
        return variable.valid() ? value(variable) : Scalar::zero();
    };
    for (std::size_t gate_index = 0; gate_index < gates_.size(); ++gate_index) {
        const std::size_t row = public_variables_.size() + gate_index;
        const Gate &gate = gates_[gate_index];
        wires[0][row] = gate.a;
        wires[1][row] = gate.b;
        wires[2][row] = gate.c;
        witness.a[row] = wire_value(gate.a);
        witness.b[row] = wire_value(gate.b);
        witness.c[row] = wire_value(gate.c);
        circuit.q_l[row] = gate.q_l;
        circuit.q_r[row] = gate.q_r;
        circuit.q_m[row] = gate.q_m;
        circuit.q_o[row] = gate.q_o;
        circuit.q_c[row] = gate.q_c;
    }

    for (std::size_t row = 0; row < used_rows; ++row) {
        const Scalar residual = circuit.q_l[row] * witness.a[row] +
                                circuit.q_r[row] * witness.b[row] +
                                circuit.q_m[row] * witness.a[row] * witness.b[row] +
                                circuit.q_o[row] * witness.c[row] + circuit.q_c[row] +
                                witness.public_input[row];
        if (residual != Scalar::zero())
            throw std::invalid_argument("PLONK witness does not satisfy arithmetic row " +
                                        std::to_string(row));
    }

    const std::size_t cells = 3 * domain_size;
    std::vector<std::uint32_t> permutation(cells);
    for (std::size_t cell = 0; cell < cells; ++cell)
        permutation[cell] = static_cast<std::uint32_t>(cell);

    std::vector<std::vector<std::uint32_t>> occurrences(variables_.size());
    for (std::size_t column = 0; column < wires.size(); ++column) {
        for (std::size_t row = 0; row < domain_size; ++row) {
            const Variable variable = wires[column][row];
            if (variable.valid()) {
                occurrences[variable.index()].push_back(
                    static_cast<std::uint32_t>(column * domain_size + row));
            }
        }
    }
    for (const auto &cycle : occurrences) {
        if (cycle.empty()) continue;
        for (std::size_t i = 0; i < cycle.size(); ++i)
            permutation[cycle[i]] = cycle[(i + 1) % cycle.size()];
    }

    for (std::size_t row = 0; row < domain_size; ++row) {
        circuit.sigma_1[row] = permutation[row];
        circuit.sigma_2[row] = permutation[domain_size + row];
        circuit.sigma_3[row] = permutation[2 * domain_size + row];
    }
    return result;
}

} // namespace nkr::plonk
