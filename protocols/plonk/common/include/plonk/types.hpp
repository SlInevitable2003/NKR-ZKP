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

struct DomainParameters {
    std::size_t domain_size = 0;
    std::size_t quotient_domain_size = 0;
    Scalar omega = Scalar::zero();
    Scalar k_1 = Scalar::zero();
    Scalar k_2 = Scalar::zero();
    Scalar coset_shift = Scalar::zero();
};

struct PolynomialData {
    std::vector<Scalar> evaluations;       // values on H
    std::vector<Scalar> coefficients;      // monomial coefficients
    std::vector<Scalar> coset_evaluations; // values on coset_shift * H_M
};

struct FixedPolynomials {
    PolynomialData q_l;
    PolynomialData q_r;
    PolynomialData q_m;
    PolynomialData q_o;
    PolynomialData q_c;
    PolynomialData sigma_1;
    PolynomialData sigma_2;
    PolynomialData sigma_3;
};

struct CircuitCommitments {
    G1 q_l;
    G1 q_r;
    G1 q_m;
    G1 q_o;
    G1 q_c;
    G1 sigma_1;
    G1 sigma_2;
    G1 sigma_3;
};

struct ProvingKey {
    Circuit circuit;
    DomainParameters domain;
    FixedPolynomials fixed;
    CircuitCommitments commitments;
    UniversalSrs srs;
};

struct VerificationKey {
    DomainParameters domain;
    CircuitCommitments commitments;
    G2 g2;
    G2 tau_g2;
};

struct Proof {
    G1 a_commitment;
    G1 b_commitment;
    G1 c_commitment;
    G1 z_commitment;
    G1 t_lo_commitment;
    G1 t_mid_commitment;
    G1 t_hi_commitment;
    Scalar a_zeta = Scalar::zero();
    Scalar b_zeta = Scalar::zero();
    Scalar c_zeta = Scalar::zero();
    Scalar sigma_1_zeta = Scalar::zero();
    Scalar sigma_2_zeta = Scalar::zero();
    Scalar z_zeta_omega = Scalar::zero();
    Scalar p_non_constant_zeta = Scalar::zero();
    Scalar t_zeta = Scalar::zero();
    G1 w_zeta;
    G1 w_zeta_omega;
};

} // namespace nkr::plonk
