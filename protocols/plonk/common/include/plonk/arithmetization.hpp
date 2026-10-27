#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "plonk/types.hpp"

namespace nkr::plonk {

enum class VariableVisibility {
    Public,
    Private,
};

class Variable {
public:
    using Index = std::uint32_t;
    static constexpr Index invalid_index = std::numeric_limits<Index>::max();

    constexpr Variable() = default;
    explicit constexpr Variable(Index index) : index_(index) {}

    constexpr bool valid() const { return index_ != invalid_index; }
    constexpr Index index() const { return index_; }

    friend constexpr bool operator==(Variable lhs, Variable rhs)
    {
        return lhs.index_ == rhs.index_;
    }

private:
    Index index_ = invalid_index;
};

struct LinearTerm {
    Scalar coefficient;
    Variable variable;
};

struct Arithmetization {
    Circuit circuit;
    Witness witness;
};

// A circuit builder for the classic three-wire PLONK gate
//
//   q_l * a + q_r * b + q_m * a * b + q_o * c + q_c + PI = 0.
//
// It deliberately does not reuse Groth16's R1CS manager: PLONK rows, wire
// occurrences, public-input rows, and copy constraints are native data here.
class PlonkishManager {
public:
    Variable public_variable(const Scalar &value);
    Variable private_variable(const Scalar &value);
    Variable constant(const Scalar &value);

    const Scalar &value(Variable variable) const;
    std::size_t num_variables() const { return variables_.size(); }
    std::size_t num_public_inputs() const { return public_variables_.size(); }
    std::size_t num_gates() const { return gates_.size(); }

    Variable add(Variable lhs, Variable rhs);
    Variable subtract(Variable lhs, Variable rhs);
    Variable multiply(Variable lhs, Variable rhs);
    Variable linear_combination(const std::vector<LinearTerm> &terms,
                                const Scalar &constant_term = Scalar::zero());

    void assert_equal(Variable lhs, Variable rhs);
    void enforce_multiplication(Variable output, Variable lhs, Variable rhs);

    void add_arithmetic_gate(Variable a,
                             Variable b,
                             Variable c,
                             const Scalar &q_l,
                             const Scalar &q_r,
                             const Scalar &q_m,
                             const Scalar &q_o,
                             const Scalar &q_c);

    Arithmetization finalize(std::size_t minimum_domain_size = 8) const;

private:
    struct VariableRecord {
        VariableVisibility visibility;
        Scalar value;
    };

    struct Gate {
        Variable a;
        Variable b;
        Variable c;
        Scalar q_l;
        Scalar q_r;
        Scalar q_m;
        Scalar q_o;
        Scalar q_c;
    };

    Variable allocate(VariableVisibility visibility, const Scalar &value);
    void validate_variable(Variable variable) const;

    std::vector<VariableRecord> variables_;
    std::vector<Variable> public_variables_;
    std::vector<Gate> gates_;
};

} // namespace nkr::plonk
