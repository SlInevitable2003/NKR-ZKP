#include "plonk/io.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "binary_archive.hpp"

namespace nkr::plonk {
namespace {

void write_circuit_data(BinaryArchive &archive, const Circuit &circuit)
{
    archive.write(circuit.domain_size);
    archive.write(circuit.rows);
    archive.write(circuit.public_inputs);
    archive.write(circuit.q_l);
    archive.write(circuit.q_r);
    archive.write(circuit.q_m);
    archive.write(circuit.q_o);
    archive.write(circuit.q_c);
    archive.write(circuit.sigma_1);
    archive.write(circuit.sigma_2);
    archive.write(circuit.sigma_3);
}

Circuit read_circuit_data(BinaryArchive &archive)
{
    Circuit circuit;
    archive.read(circuit.domain_size);
    archive.read(circuit.rows);
    archive.read(circuit.public_inputs);
    archive.read(circuit.q_l);
    archive.read(circuit.q_r);
    archive.read(circuit.q_m);
    archive.read(circuit.q_o);
    archive.read(circuit.q_c);
    archive.read(circuit.sigma_1);
    archive.read(circuit.sigma_2);
    archive.read(circuit.sigma_3);
    return circuit;
}

void write_witness_data(BinaryArchive &archive, const Witness &witness)
{
    archive.write(witness.a);
    archive.write(witness.b);
    archive.write(witness.c);
    archive.write(witness.public_input);
}

Witness read_witness_data(BinaryArchive &archive)
{
    Witness witness;
    archive.read(witness.a);
    archive.read(witness.b);
    archive.read(witness.c);
    archive.read(witness.public_input);
    return witness;
}

void write_srs_data(BinaryArchive &archive, const UniversalSrs &srs)
{
    archive.write(srs.g1_powers);
    archive.write(srs.g2);
    archive.write(srs.tau_g2);
}

UniversalSrs read_srs_data(BinaryArchive &archive)
{
    UniversalSrs srs;
    archive.read(srs.g1_powers);
    archive.read(srs.g2);
    archive.read(srs.tau_g2);
    return srs;
}

void write_domain(BinaryArchive &archive, const DomainParameters &domain)
{
    archive.write(domain.domain_size);
    archive.write(domain.quotient_domain_size);
    archive.write(domain.omega);
    archive.write(domain.k_1);
    archive.write(domain.k_2);
    archive.write(domain.coset_shift);
}

DomainParameters read_domain(BinaryArchive &archive)
{
    DomainParameters domain;
    archive.read(domain.domain_size);
    archive.read(domain.quotient_domain_size);
    archive.read(domain.omega);
    archive.read(domain.k_1);
    archive.read(domain.k_2);
    archive.read(domain.coset_shift);
    return domain;
}

void write_polynomial(BinaryArchive &archive, const PolynomialData &polynomial)
{
    archive.write(polynomial.evaluations);
    archive.write(polynomial.coefficients);
    archive.write(polynomial.coset_evaluations);
}

PolynomialData read_polynomial(BinaryArchive &archive)
{
    PolynomialData polynomial;
    archive.read(polynomial.evaluations);
    archive.read(polynomial.coefficients);
    archive.read(polynomial.coset_evaluations);
    return polynomial;
}

void write_fixed(BinaryArchive &archive, const FixedPolynomials &fixed)
{
    write_polynomial(archive, fixed.q_l);
    write_polynomial(archive, fixed.q_r);
    write_polynomial(archive, fixed.q_m);
    write_polynomial(archive, fixed.q_o);
    write_polynomial(archive, fixed.q_c);
    write_polynomial(archive, fixed.sigma_1);
    write_polynomial(archive, fixed.sigma_2);
    write_polynomial(archive, fixed.sigma_3);
}

FixedPolynomials read_fixed(BinaryArchive &archive)
{
    FixedPolynomials fixed;
    fixed.q_l = read_polynomial(archive);
    fixed.q_r = read_polynomial(archive);
    fixed.q_m = read_polynomial(archive);
    fixed.q_o = read_polynomial(archive);
    fixed.q_c = read_polynomial(archive);
    fixed.sigma_1 = read_polynomial(archive);
    fixed.sigma_2 = read_polynomial(archive);
    fixed.sigma_3 = read_polynomial(archive);
    return fixed;
}

void write_commitments(BinaryArchive &archive, const CircuitCommitments &commitments)
{
    archive.write(commitments.q_l);
    archive.write(commitments.q_r);
    archive.write(commitments.q_m);
    archive.write(commitments.q_o);
    archive.write(commitments.q_c);
    archive.write(commitments.sigma_1);
    archive.write(commitments.sigma_2);
    archive.write(commitments.sigma_3);
}

CircuitCommitments read_commitments(BinaryArchive &archive)
{
    CircuitCommitments commitments;
    archive.read(commitments.q_l);
    archive.read(commitments.q_r);
    archive.read(commitments.q_m);
    archive.read(commitments.q_o);
    archive.read(commitments.q_c);
    archive.read(commitments.sigma_1);
    archive.read(commitments.sigma_2);
    archive.read(commitments.sigma_3);
    return commitments;
}

void strip_suffix(std::string &filename, const std::string &suffix)
{
    if (filename.size() >= suffix.size() &&
        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
        filename.resize(filename.size() - suffix.size());
    }
}

} // namespace

ArtifactPaths artifact_paths(const std::filesystem::path &directory, std::string filename)
{
    const std::array<std::string, 6> suffixes = {
        ".witness.bin", ".srs.bin", ".pk.bin", ".vk.bin", ".proof.bin", ".bin"};
    for (const std::string &suffix : suffixes) {
        const std::size_t old_size = filename.size();
        strip_suffix(filename, suffix);
        if (filename.size() != old_size) break;
    }
    const std::filesystem::path stem = directory / filename;
    return {stem.string() + ".bin",
            stem.string() + ".witness.bin",
            stem.string() + ".srs.bin",
            stem.string() + ".pk.bin",
            stem.string() + ".vk.bin",
            stem.string() + ".proof.bin"};
}

Circuit read_circuit(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    return read_circuit_data(archive);
}

void write_circuit(const std::filesystem::path &path, const Circuit &circuit)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    write_circuit_data(archive, circuit);
}

Witness read_witness(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    return read_witness_data(archive);
}

void write_witness(const std::filesystem::path &path, const Witness &witness)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    write_witness_data(archive, witness);
}

UniversalSrs read_srs(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    return read_srs_data(archive);
}

void write_srs(const std::filesystem::path &path, const UniversalSrs &srs)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    write_srs_data(archive, srs);
}

ProvingKey read_proving_key(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    ProvingKey key;
    key.circuit = read_circuit_data(archive);
    key.domain = read_domain(archive);
    key.fixed = read_fixed(archive);
    key.commitments = read_commitments(archive);
    key.srs = read_srs_data(archive);
    return key;
}

void write_proving_key(const std::filesystem::path &path, const ProvingKey &key)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    write_circuit_data(archive, key.circuit);
    write_domain(archive, key.domain);
    write_fixed(archive, key.fixed);
    write_commitments(archive, key.commitments);
    write_srs_data(archive, key.srs);
}

VerificationKey read_verification_key(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    VerificationKey key;
    key.domain = read_domain(archive);
    key.commitments = read_commitments(archive);
    archive.read(key.g2);
    archive.read(key.tau_g2);
    return key;
}

void write_verification_key(const std::filesystem::path &path, const VerificationKey &key)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    write_domain(archive, key.domain);
    write_commitments(archive, key.commitments);
    archive.write(key.g2);
    archive.write(key.tau_g2);
}

Proof read_proof(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    Proof proof;
    archive.read(proof.encoded);
    return proof;
}

void write_proof(const std::filesystem::path &path, const Proof &proof)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    archive.write(proof.encoded);
}

} // namespace nkr::plonk
