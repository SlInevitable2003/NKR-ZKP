#pragma once

#include <cstddef>
#include <vector>

#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"
#include "r1cs/spmat.hpp"

namespace nkr::groth16 {

using Curve = libsnark::default_r1cs_ppzksnark_pp;
using Scalar = libff::Fr<Curve>;
using G1 = libff::G1<Curve>;
using G2 = libff::G2<Curve>;
using GT = libff::GT<Curve>;

struct Circuit {
    std::size_t public_inputs = 0;
    CsrMatrix<Scalar> a;
    CsrMatrix<Scalar> b;
    CsrMatrix<Scalar> c;

    std::size_t constraints() const { return a.num_rows; }
    std::size_t variables() const { return a.num_cols > 0 ? a.num_cols - 1 : 0; }
};

struct ProvingKey {
    std::vector<G1> a_query;
    std::vector<G1> b_g1_query;
    std::vector<G2> b_g2_query;
    std::vector<G1> l_query;
    std::vector<G1> h_query;
    G1 alpha_g1;
    G1 beta_g1;
    G2 beta_g2;
    G1 delta_g1;
    G2 delta_g2;
};

struct VerificationKey {
    GT alpha_g1_beta_g2;
    G2 gamma_g2;
    G2 delta_g2;
    std::vector<G1> ic;
};

struct RawProof {
    G1 ar;
    G1 bs1;
    G1 zk;
    G1 qz;
    G2 bs2;
};

struct Proof {
    G1 a;
    G2 b;
    G1 c;
};

} // namespace nkr::groth16

