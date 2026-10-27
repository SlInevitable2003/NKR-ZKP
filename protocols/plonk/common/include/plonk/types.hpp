#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"

namespace nkr::plonk {

using Curve = libsnark::default_r1cs_ppzksnark_pp;
using Scalar = libff::Fr<Curve>;
using G1 = libff::G1<Curve>;
using G2 = libff::G2<Curve>;

struct Circuit {
    std::size_t domain_size = 0;
    std::size_t rows = 0; // non-padding rows, including public-input rows
    std::size_t public_inputs = 0;
    std::vector<Scalar> q_l;
    std::vector<Scalar> q_r;
    std::vector<Scalar> q_m;
    std::vector<Scalar> q_o;
    std::vector<Scalar> q_c;
    // Each entry is a destination cell encoded as column * domain_size + row.
    std::vector<std::uint32_t> sigma_1;
    std::vector<std::uint32_t> sigma_2;
    std::vector<std::uint32_t> sigma_3;
};

struct Witness {
    std::vector<Scalar> a;
    std::vector<Scalar> b;
    std::vector<Scalar> c;
    std::vector<Scalar> public_input; // full PI column over H; public rows come first
};

struct UniversalSrs {
    std::vector<G1> g1_powers;
    G2 g2;
    G2 tau_g2;
};

struct ProvingKey {
    Circuit circuit;
    UniversalSrs srs;
};

struct VerificationKey {
    std::size_t domain_size = 0;
    G1 q_l;
    G1 q_r;
    G1 q_m;
    G1 q_o;
    G1 q_c;
    G1 sigma_1;
    G1 sigma_2;
    G1 sigma_3;
    G2 g2;
    G2 tau_g2;
};

// The proof container is intentionally left opaque until the transcript,
// canonical encoding, and exact low-level protocol document are frozen.
struct Proof {
    std::vector<std::uint8_t> encoded;
};

} // namespace nkr::plonk
