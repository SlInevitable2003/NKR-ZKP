#include "plonkish/arithmetization.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace nkr::plonk;
using namespace Plonkish;

namespace {

Scalar cell_value(const Witness &witness, std::size_t cell)
{
    const std::size_t n = witness.a.size();
    if (cell < n) return witness.a[cell];
    if (cell < 2 * n) return witness.b[cell - n];
    return witness.c[cell - 2 * n];
}

void check_arithmetization(const Arithmetization &output)
{
    const Circuit &circuit = output.circuit;
    const Witness &witness = output.witness;
    const std::size_t n = circuit.domain_size;
    assert(n >= circuit.rows);
    assert((n & (n - 1)) == 0);
    assert(circuit.q_l.size() == n);
    assert(circuit.q_r.size() == n);
    assert(circuit.q_m.size() == n);
    assert(circuit.q_o.size() == n);
    assert(circuit.q_c.size() == n);
    assert(witness.a.size() == n);
    assert(witness.b.size() == n);
    assert(witness.c.size() == n);
    assert(witness.public_input.size() == n);

    for (std::size_t row = 0; row < n; ++row) {
        const Scalar residual = circuit.q_l[row] * witness.a[row] +
                                circuit.q_r[row] * witness.b[row] +
                                circuit.q_m[row] * witness.a[row] * witness.b[row] +
                                circuit.q_o[row] * witness.c[row] + circuit.q_c[row] +
                                witness.public_input[row];
        assert(residual == Scalar::zero());
    }

    std::vector<std::uint32_t> sigma;
    sigma.insert(sigma.end(), circuit.sigma_1.begin(), circuit.sigma_1.end());
    sigma.insert(sigma.end(), circuit.sigma_2.begin(), circuit.sigma_2.end());
    sigma.insert(sigma.end(), circuit.sigma_3.begin(), circuit.sigma_3.end());
    assert(sigma.size() == 3 * n);
    std::vector<bool> seen(3 * n, false);
    for (std::size_t cell = 0; cell < sigma.size(); ++cell) {
        assert(sigma[cell] < sigma.size());
        assert(!seen[sigma[cell]]);
        seen[sigma[cell]] = true;
        assert(cell_value(witness, cell) == cell_value(witness, sigma[cell]));
    }
}

} // namespace

int main()
{
    Curve::init_public_params();

    PlonkishManager manager;
    const Variable x = manager.public_variable(Scalar(3));
    const Variable y = manager.public_variable(Scalar(4));
    const Variable sum = manager.add(x, y);
    const Variable difference = manager.subtract(sum, x);
    const Variable product = manager.multiply(sum, difference);
    const Variable expected = manager.linear_combination(
        {{Scalar(7), y}}, Scalar::zero());
    manager.assert_equal(product, expected);

    const Arithmetization output = manager.finalize();
    assert(output.circuit.public_inputs == 2);
    assert(output.circuit.rows == 7);
    assert(output.circuit.domain_size == 8);
    assert(output.witness.public_input[0] == Scalar(3));
    assert(output.witness.public_input[1] == Scalar(4));
    // x occurs in the public-input row, the addition gate's A wire, and the
    // subtraction gate's B wire. Those cells must form one copy cycle.
    const std::size_t n = output.circuit.domain_size;
    assert(output.circuit.sigma_1[0] == 2);
    assert(output.circuit.sigma_1[2] == n + 3);
    assert(output.circuit.sigma_2[3] == 0);
    check_arithmetization(output);

    PlonkishManager combination_manager;
    const Variable u = combination_manager.public_variable(Scalar(2));
    const Variable v = combination_manager.private_variable(Scalar(5));
    const Variable combination = combination_manager.linear_combination(
        {{Scalar(2), u}, {Scalar(3), v}}, Scalar(4));
    const Variable combination_expected = combination_manager.constant(Scalar(23));
    combination_manager.assert_equal(combination, combination_expected);
    check_arithmetization(combination_manager.finalize());

    PlonkishManager invalid;
    const Variable a = invalid.private_variable(Scalar(2));
    const Variable b = invalid.private_variable(Scalar(3));
    const Variable wrong = invalid.private_variable(Scalar(7));
    invalid.enforce_multiplication(wrong, a, b);
    bool rejected = false;
    try {
        (void)invalid.finalize();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert(rejected);
    return 0;
}
