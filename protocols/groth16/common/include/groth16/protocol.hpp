#pragma once

#include <string>
#include <vector>

#include "groth16/types.hpp"

namespace nkr::groth16 {

bool validate_circuit(const Circuit &circuit, std::string &error);
bool validate_proving_key(const Circuit &circuit, const ProvingKey &key, std::string &error);
bool check_witness(const Circuit &circuit, const std::vector<Scalar> &witness);
Proof blind_proof(const ProvingKey &proving_key, const RawProof &raw);
bool verify(const VerificationKey &verification_key,
            const std::vector<Scalar> &public_input,
            const Proof &proof);

} // namespace nkr::groth16
