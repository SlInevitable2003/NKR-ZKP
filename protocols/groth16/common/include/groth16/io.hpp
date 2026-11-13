#pragma once

#include "nkr/filesystem.hpp"
#include <string>
#include <vector>

#include "binary_archive.hpp"
#include "groth16/types.hpp"

namespace nkr::groth16 {

struct ArtifactPaths {
    nkr::fs::path r1cs;
    nkr::fs::path witness;
    nkr::fs::path proving_key;
    nkr::fs::path verification_key;
    nkr::fs::path proof;
};

ArtifactPaths artifact_paths(const nkr::fs::path &directory, std::string filename);
Circuit read_circuit(const nkr::fs::path &path);
void write_circuit(const nkr::fs::path &path, const Circuit &circuit);
std::vector<Scalar> read_witness(const nkr::fs::path &path);
void write_witness(const nkr::fs::path &path, const std::vector<Scalar> &witness);
ProvingKey read_proving_key(const nkr::fs::path &path);
void write_proving_key(const nkr::fs::path &path, const ProvingKey &key);
VerificationKey read_verification_key(const nkr::fs::path &path);
void write_verification_key(const nkr::fs::path &path, const VerificationKey &key);
Proof read_proof(const nkr::fs::path &path);
void write_proof(const nkr::fs::path &path, const Proof &proof);

} // namespace nkr::groth16

