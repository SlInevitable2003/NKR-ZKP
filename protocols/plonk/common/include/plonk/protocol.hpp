#pragma once

#include <string>

#include "plonk/backend.hpp"

namespace nkr::plonk {

[[noreturn]] void throw_scaffold_only();

bool validate_circuit(const Circuit &circuit, std::string &error);
bool validate_srs(const UniversalSrs &srs, std::size_t domain_size, std::string &error);

void setup(Backend &backend,
           const Circuit &circuit,
           const UniversalSrs &srs,
           ProvingKey &proving_key,
           VerificationKey &verification_key);

Proof prove(Backend &backend,
            const ProvingKey &proving_key,
            const Witness &witness);

bool verify(const VerificationKey &verification_key,
            const std::vector<Scalar> &public_input,
            const Proof &proof);

} // namespace nkr::plonk
