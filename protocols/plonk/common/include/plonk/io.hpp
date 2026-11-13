#pragma once

#include "nkr/filesystem.hpp"
#include <string>

#include "plonk/types.hpp"

namespace nkr::plonk {

struct ArtifactPaths {
    nkr::fs::path circuit;
    nkr::fs::path witness;
    nkr::fs::path srs;
    nkr::fs::path proving_key;
    nkr::fs::path verification_key;
    nkr::fs::path proof;
};

ArtifactPaths artifact_paths(const nkr::fs::path &directory, std::string filename);

Circuit read_circuit(const nkr::fs::path &path);
void write_circuit(const nkr::fs::path &path, const Circuit &circuit);

Witness read_witness(const nkr::fs::path &path);
void write_witness(const nkr::fs::path &path, const Witness &witness);

UniversalSrs read_srs(const nkr::fs::path &path);
void write_srs(const nkr::fs::path &path, const UniversalSrs &srs);

ProvingKey read_proving_key(const nkr::fs::path &path);
void write_proving_key(const nkr::fs::path &path, const ProvingKey &key);

VerificationKey read_verification_key(const nkr::fs::path &path);
void write_verification_key(const nkr::fs::path &path, const VerificationKey &key);

Proof read_proof(const nkr::fs::path &path);
void write_proof(const nkr::fs::path &path, const Proof &proof);

} // namespace nkr::plonk
