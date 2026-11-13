#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "binary_archive.hpp"
#include "groth16/types.hpp"

namespace nkr::groth16 {

struct ArtifactPaths {
    std::filesystem::path r1cs;
    std::filesystem::path witness;
    std::filesystem::path proving_key;
    std::filesystem::path verification_key;
    std::filesystem::path proof;
};

ArtifactPaths artifact_paths(const std::filesystem::path &directory, std::string filename);
Circuit read_circuit(const std::filesystem::path &path);
void write_circuit(const std::filesystem::path &path, const Circuit &circuit);
std::vector<Scalar> read_witness(const std::filesystem::path &path);
void write_witness(const std::filesystem::path &path, const std::vector<Scalar> &witness);
ProvingKey read_proving_key(const std::filesystem::path &path);
void write_proving_key(const std::filesystem::path &path, const ProvingKey &key);
VerificationKey read_verification_key(const std::filesystem::path &path);
void write_verification_key(const std::filesystem::path &path, const VerificationKey &key);
Proof read_proof(const std::filesystem::path &path);
void write_proof(const std::filesystem::path &path, const Proof &proof);

} // namespace nkr::groth16

