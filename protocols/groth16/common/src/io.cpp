#include "groth16/io.hpp"

namespace nkr::groth16 {

ArtifactPaths artifact_paths(const std::filesystem::path &directory, std::string filename)
{
    if (filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".bin") == 0)
        filename.resize(filename.size() - 4);
    return {directory / (filename + ".bin"),
            directory / (filename + ".witness.bin"),
            directory / (filename + ".pk.bin"),
            directory / (filename + ".vk.bin"),
            directory / (filename + ".proof.bin")};
}

Circuit read_circuit(const std::filesystem::path &path)
{
    BinaryArchive archive;
    archive.open_for_read(path.string());
    Circuit circuit;
    std::size_t columns;
    archive.read(circuit.public_inputs);
    archive.read(columns);
    archive.read(circuit.a.row_ptr); archive.read(circuit.a.col_idx); archive.read(circuit.a.values);
    archive.read(circuit.b.row_ptr); archive.read(circuit.b.col_idx); archive.read(circuit.b.values);
    archive.read(circuit.c.row_ptr); archive.read(circuit.c.col_idx); archive.read(circuit.c.values);
    circuit.a.num_rows = circuit.a.row_ptr.size() - 1; circuit.a.num_cols = columns;
    circuit.b.num_rows = circuit.b.row_ptr.size() - 1; circuit.b.num_cols = columns;
    circuit.c.num_rows = circuit.c.row_ptr.size() - 1; circuit.c.num_cols = columns;
    return circuit;
}

void write_circuit(const std::filesystem::path &path, const Circuit &circuit)
{
    BinaryArchive archive;
    archive.open_for_write(path.string());
    archive.write(circuit.public_inputs); archive.write(circuit.a.num_cols);
    archive.write(circuit.a.row_ptr); archive.write(circuit.a.col_idx); archive.write(circuit.a.values);
    archive.write(circuit.b.row_ptr); archive.write(circuit.b.col_idx); archive.write(circuit.b.values);
    archive.write(circuit.c.row_ptr); archive.write(circuit.c.col_idx); archive.write(circuit.c.values);
}

std::vector<Scalar> read_witness(const std::filesystem::path &path)
{
    BinaryArchive archive; archive.open_for_read(path.string());
    std::vector<Scalar> witness; archive.read(witness); return witness;
}

void write_witness(const std::filesystem::path &path, const std::vector<Scalar> &witness)
{
    BinaryArchive archive; archive.open_for_write(path.string()); archive.write(witness);
}

ProvingKey read_proving_key(const std::filesystem::path &path)
{
    BinaryArchive archive; archive.open_for_read(path.string()); ProvingKey key;
    archive.read(key.a_query); archive.read(key.b_g1_query); archive.read(key.b_g2_query);
    archive.read(key.l_query); archive.read(key.h_query);
    archive.read(key.alpha_g1); archive.read(key.beta_g1); archive.read(key.beta_g2);
    archive.read(key.delta_g1); archive.read(key.delta_g2); return key;
}

void write_proving_key(const std::filesystem::path &path, const ProvingKey &key)
{
    BinaryArchive archive; archive.open_for_write(path.string());
    archive.write(key.a_query); archive.write(key.b_g1_query); archive.write(key.b_g2_query);
    archive.write(key.l_query); archive.write(key.h_query);
    archive.write(key.alpha_g1); archive.write(key.beta_g1); archive.write(key.beta_g2);
    archive.write(key.delta_g1); archive.write(key.delta_g2);
}

VerificationKey read_verification_key(const std::filesystem::path &path)
{
    BinaryArchive archive; archive.open_for_read(path.string()); VerificationKey key;
    archive.read(key.alpha_g1_beta_g2); archive.read(key.gamma_g2); archive.read(key.delta_g2);
    archive.read(key.ic); return key;
}

void write_verification_key(const std::filesystem::path &path, const VerificationKey &key)
{
    BinaryArchive archive; archive.open_for_write(path.string());
    archive.write(key.alpha_g1_beta_g2); archive.write(key.gamma_g2); archive.write(key.delta_g2);
    archive.write(key.ic);
}

Proof read_proof(const std::filesystem::path &path)
{
    BinaryArchive archive; archive.open_for_read(path.string()); Proof proof;
    archive.read(proof.a); archive.read(proof.b); archive.read(proof.c); return proof;
}

void write_proof(const std::filesystem::path &path, const Proof &proof)
{
    BinaryArchive archive; archive.open_for_write(path.string());
    archive.write(proof.a); archive.write(proof.b); archive.write(proof.c);
}

} // namespace nkr::groth16

