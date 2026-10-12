#include "groth16/protocol.hpp"

#include <iostream>

#include "libff/algebra/scalar_multiplication/multiexp.hpp"

namespace nkr::groth16 {

bool validate_circuit(const Circuit &circuit, std::string &error)
{
    const auto valid_matrix = [&](const CsrMatrix<Scalar> &matrix, const char *name) {
        if (matrix.num_rows == 0 || matrix.num_cols == 0) {
            error = std::string(name) + " has an empty shape";
            return false;
        }
        if (matrix.row_ptr.size() != matrix.num_rows + 1 || matrix.row_ptr.front() != 0 ||
            matrix.row_ptr.back() != matrix.values.size() || matrix.col_idx.size() != matrix.values.size()) {
            error = std::string(name) + " has invalid CSR offsets";
            return false;
        }
        for (std::size_t row = 1; row < matrix.row_ptr.size(); ++row) {
            if (matrix.row_ptr[row] < matrix.row_ptr[row - 1]) {
                error = std::string(name) + " has non-monotonic CSR offsets";
                return false;
            }
        }
        for (const auto column : matrix.col_idx) {
            if (column >= matrix.num_cols) {
                error = std::string(name) + " contains an out-of-range column";
                return false;
            }
        }
        return true;
    };

    if (!valid_matrix(circuit.a, "A") || !valid_matrix(circuit.b, "B") ||
        !valid_matrix(circuit.c, "C")) return false;
    if (circuit.a.num_rows != circuit.b.num_rows || circuit.a.num_rows != circuit.c.num_rows ||
        circuit.a.num_cols != circuit.b.num_cols || circuit.a.num_cols != circuit.c.num_cols) {
        error = "R1CS matrices have different shapes";
        return false;
    }
    if (circuit.public_inputs + 1 > circuit.a.num_cols) {
        error = "public input count exceeds the variable count";
        return false;
    }
    if ((circuit.constraints() & (circuit.constraints() - 1)) != 0) {
        error = "constraint count must be a power of two";
        return false;
    }
    return true;
}

bool validate_proving_key(const Circuit &circuit, const ProvingKey &key, std::string &error)
{
    const std::size_t columns = circuit.a.num_cols;
    if (key.a_query.size() != columns || key.b_g1_query.size() != columns ||
        key.b_g2_query.size() != columns || key.l_query.size() != columns) {
        error = "query length does not match the circuit variable count";
        return false;
    }
    if (key.h_query.size() != circuit.constraints()) {
        error = "H query length does not match the constraint count";
        return false;
    }
    return true;
}

bool check_witness(const Circuit &circuit, const std::vector<Scalar> &witness)
{
    if (witness.size() != circuit.a.num_cols) return false;
    for (std::size_t row = 0; row < circuit.constraints(); ++row) {
        Scalar av = Scalar::zero(), bv = Scalar::zero(), cv = Scalar::zero();
        for (std::size_t j = circuit.a.row_ptr[row]; j < circuit.a.row_ptr[row + 1]; ++j)
            av += circuit.a.values[j] * witness[circuit.a.col_idx[j]];
        for (std::size_t j = circuit.b.row_ptr[row]; j < circuit.b.row_ptr[row + 1]; ++j)
            bv += circuit.b.values[j] * witness[circuit.b.col_idx[j]];
        for (std::size_t j = circuit.c.row_ptr[row]; j < circuit.c.row_ptr[row + 1]; ++j)
            cv += circuit.c.values[j] * witness[circuit.c.col_idx[j]];
        if (av * bv != cv) return false;
    }
    return true;
}

Proof blind_proof(const ProvingKey &key, const RawProof &raw)
{
    const Scalar r = Scalar::random_element();
    const Scalar s = Scalar::random_element();
    const G1 a = key.alpha_g1 + raw.ar + r * key.delta_g1;
    const G1 b_g1 = key.beta_g1 + raw.bs1 + s * key.delta_g1;
    return {a,
            key.beta_g2 + raw.bs2 + s * key.delta_g2,
            raw.qz + raw.zk + s * a + r * b_g1 - (r * s) * key.delta_g1};
}

bool verify(const VerificationKey &key, const std::vector<Scalar> &public_input, const Proof &proof)
{
    if (key.ic.size() != public_input.size() + 1) return false;
    G1 accumulated = key.ic[0];
    if (!public_input.empty()) {
        accumulated = accumulated +
            libff::multi_exp<G1, Scalar, libff::multi_exp_method::multi_exp_method_BDLO12>(
                key.ic.begin() + 1, key.ic.end(), public_input.begin(), public_input.end(), 1);
    }
    return Curve::reduced_pairing(proof.a, proof.b) ==
           key.alpha_g1_beta_g2 * Curve::reduced_pairing(accumulated, key.gamma_g2) *
               Curve::reduced_pairing(proof.c, key.delta_g2);
}

} // namespace nkr::groth16
