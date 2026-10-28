#pragma once

#include <string>
#include <vector>

#include "plonk/types.hpp"

namespace nkr::plonk {

UniversalSrs generate_srs(std::size_t max_degree);
bool validate_circuit(const Circuit &circuit, std::string &error);
bool validate_srs(const UniversalSrs &srs, std::size_t domain_size, std::string &error);
bool validate_proving_key(const ProvingKey &key, std::string &error);
bool validate_witness(const ProvingKey &key, const Witness &witness, std::string &error);

bool verify(const VerificationKey &verification_key,
            const std::vector<Scalar> &public_input,
            const Proof &proof);

} // namespace nkr::plonk
