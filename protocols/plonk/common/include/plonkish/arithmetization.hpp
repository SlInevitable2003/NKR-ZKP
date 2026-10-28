#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "plonk/types.hpp"

namespace Plonkish {

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
    nkr::plonk::Scalar coefficient;
    Variable variable;
};

struct Arithmetization {
    nkr::plonk::Circuit circuit;
    nkr::plonk::Witness witness;
};

// A circuit builder for the classic three-wire PLONK gate
//   q_l * a + q_r * b + q_m * a * b + q_o * c + q_c + PI = 0.
class PlonkishManager {
public:
    Variable public_variable(const nkr::plonk::Scalar &value);
    Variable private_variable(const nkr::plonk::Scalar &value);
    Variable constant(const nkr::plonk::Scalar &value);

    const nkr::plonk::Scalar &value(Variable variable) const;
    std::size_t num_variables() const { return variables_.size(); }
    std::size_t num_public_inputs() const { return public_variables_.size(); }
    std::size_t num_gates() const { return gates_.size(); }

    Variable add(Variable lhs, Variable rhs);
    Variable subtract(Variable lhs, Variable rhs);
    Variable multiply(Variable lhs, Variable rhs);
    Variable linear_combination(const std::vector<LinearTerm> &terms,
                                const nkr::plonk::Scalar &constant_term =
                                    nkr::plonk::Scalar::zero());

    void assert_equal(Variable lhs, Variable rhs);
    void enforce_multiplication(Variable output, Variable lhs, Variable rhs);

    void add_arithmetic_gate(Variable a,
                             Variable b,
                             Variable c,
                             const nkr::plonk::Scalar &q_l,
                             const nkr::plonk::Scalar &q_r,
                             const nkr::plonk::Scalar &q_m,
                             const nkr::plonk::Scalar &q_o,
                             const nkr::plonk::Scalar &q_c);

    Arithmetization finalize(std::size_t minimum_domain_size = 8) const;

private:
    struct VariableRecord {
        VariableVisibility visibility;
        nkr::plonk::Scalar value;
    };

    struct Gate {
        Variable a;
        Variable b;
        Variable c;
        nkr::plonk::Scalar q_l;
        nkr::plonk::Scalar q_r;
        nkr::plonk::Scalar q_m;
        nkr::plonk::Scalar q_o;
        nkr::plonk::Scalar q_c;
    };

    Variable allocate(VariableVisibility visibility, const nkr::plonk::Scalar &value);
    void validate_variable(Variable variable) const;

    std::vector<VariableRecord> variables_;
    std::vector<Variable> public_variables_;
    std::vector<Gate> gates_;
};

} // namespace Plonkish
