#include "plonk/io.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace nkr::plonk {
namespace {

constexpr std::array<char, 8> kMagic = {'N', 'K', 'R', 'P', 'L', 'K', '1', '\n'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxElements = std::uint64_t{1} << 24;
constexpr std::uint64_t kMaxTextBytes = std::uint64_t{1} << 20;

class Writer {
public:
    Writer(const std::filesystem::path &path, const std::string &type)
        : stream_(path, std::ios::binary | std::ios::trunc)
    {
        if (!stream_) throw std::runtime_error("cannot open file for writing: " + path.string());
        stream_.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        write_u32(kVersion);
        write_string(type);
    }

    void write_u32(const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            stream_.put(static_cast<char>((value >> shift) & 0xff));
        check();
    }

    void write_u64(const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            stream_.put(static_cast<char>((value >> shift) & 0xff));
        check();
    }

    void write_string(const std::string &value)
    {
        write_u64(value.size());
        stream_.write(value.data(), static_cast<std::streamsize>(value.size()));
        check();
    }

    template <typename T>
    void write_text(const T &value)
    {
        std::ostringstream output;
        output << value;
        if (!output) throw std::runtime_error("failed to encode PLONK value");
        write_string(output.str());
    }

private:
    void check()
    {
        if (!stream_) throw std::runtime_error("failed to write PLONK artifact");
    }

    std::ofstream stream_;
};

class Reader {
public:
    Reader(const std::filesystem::path &path, const std::string &expected_type)
        : stream_(path, std::ios::binary)
    {
        if (!stream_) throw std::runtime_error("cannot open file for reading: " + path.string());
        std::array<char, kMagic.size()> magic{};
        stream_.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!stream_ || magic != kMagic || read_u32() != kVersion)
            throw std::runtime_error("incompatible or truncated PLONK artifact: " + path.string());
        const std::string actual_type = read_string();
        if (actual_type != expected_type)
            throw std::runtime_error("unexpected PLONK artifact type: " + actual_type);
    }

    std::uint32_t read_u32()
    {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const int byte = stream_.get();
            if (byte == std::char_traits<char>::eof()) unexpected_end();
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << shift;
        }
        return value;
    }

    std::uint64_t read_u64()
    {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            const int byte = stream_.get();
            if (byte == std::char_traits<char>::eof()) unexpected_end();
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << shift;
        }
        return value;
    }

    std::size_t read_size()
    {
        const std::uint64_t value = read_u64();
        if (value > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("PLONK artifact size does not fit this platform");
        return static_cast<std::size_t>(value);
    }

    std::string read_string()
    {
        const std::uint64_t length = read_u64();
        if (length > kMaxTextBytes)
            throw std::runtime_error("PLONK artifact text field exceeds the size limit");
        std::string value(static_cast<std::size_t>(length), '\0');
        stream_.read(value.data(), static_cast<std::streamsize>(length));
        if (!stream_) unexpected_end();
        return value;
    }

    template <typename T>
    T read_text()
    {
        const std::string encoded = read_string();
        std::istringstream input(encoded);
        T value;
        input >> value;
        if (!input) throw std::runtime_error("invalid value in PLONK artifact");
        input >> std::ws;
        if (!input.eof()) throw std::runtime_error("trailing data in PLONK artifact value");
        return value;
    }

    std::size_t read_count()
    {
        const std::uint64_t count = read_u64();
        if (count > kMaxElements)
            throw std::runtime_error("PLONK artifact vector exceeds the size limit");
        return static_cast<std::size_t>(count);
    }

    void finish()
    {
        if (stream_.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("trailing bytes in PLONK artifact");
    }

private:
    [[noreturn]] static void unexpected_end()
    {
        throw std::runtime_error("unexpected end of PLONK artifact");
    }

    std::ifstream stream_;
};

template <typename T>
void write_text_vector(Writer &writer, const std::vector<T> &values)
{
    writer.write_u64(values.size());
    for (const T &value : values) writer.write_text(value);
}

template <typename T>
std::vector<T> read_text_vector(Reader &reader)
{
    const std::size_t count = reader.read_count();
    std::vector<T> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) values.push_back(reader.read_text<T>());
    return values;
}

void write_index_vector(Writer &writer, const std::vector<std::uint32_t> &values)
{
    writer.write_u64(values.size());
    for (const std::uint32_t value : values) writer.write_u32(value);
}

std::vector<std::uint32_t> read_index_vector(Reader &reader)
{
    const std::size_t count = reader.read_count();
    std::vector<std::uint32_t> values(count);
    for (std::uint32_t &value : values) value = reader.read_u32();
    return values;
}

void write_bytes(Writer &writer, const std::vector<std::uint8_t> &values)
{
    writer.write_u64(values.size());
    for (const std::uint8_t value : values) writer.write_u32(value);
}

std::vector<std::uint8_t> read_bytes(Reader &reader)
{
    const std::size_t count = reader.read_count();
    std::vector<std::uint8_t> values(count);
    for (std::uint8_t &value : values) {
        const std::uint32_t encoded = reader.read_u32();
        if (encoded > std::numeric_limits<std::uint8_t>::max())
            throw std::runtime_error("invalid byte in PLONK artifact");
        value = static_cast<std::uint8_t>(encoded);
    }
    return values;
}

void write_circuit_data(Writer &writer, const Circuit &circuit)
{
    writer.write_u64(circuit.domain_size);
    writer.write_u64(circuit.rows);
    writer.write_u64(circuit.public_inputs);
    write_text_vector(writer, circuit.q_l);
    write_text_vector(writer, circuit.q_r);
    write_text_vector(writer, circuit.q_m);
    write_text_vector(writer, circuit.q_o);
    write_text_vector(writer, circuit.q_c);
    write_index_vector(writer, circuit.sigma_1);
    write_index_vector(writer, circuit.sigma_2);
    write_index_vector(writer, circuit.sigma_3);
}

Circuit read_circuit_data(Reader &reader)
{
    Circuit circuit;
    circuit.domain_size = reader.read_size();
    circuit.rows = reader.read_size();
    circuit.public_inputs = reader.read_size();
    circuit.q_l = read_text_vector<Scalar>(reader);
    circuit.q_r = read_text_vector<Scalar>(reader);
    circuit.q_m = read_text_vector<Scalar>(reader);
    circuit.q_o = read_text_vector<Scalar>(reader);
    circuit.q_c = read_text_vector<Scalar>(reader);
    circuit.sigma_1 = read_index_vector(reader);
    circuit.sigma_2 = read_index_vector(reader);
    circuit.sigma_3 = read_index_vector(reader);
    return circuit;
}

void write_witness_data(Writer &writer, const Witness &witness)
{
    write_text_vector(writer, witness.a);
    write_text_vector(writer, witness.b);
    write_text_vector(writer, witness.c);
    write_text_vector(writer, witness.public_input);
}

Witness read_witness_data(Reader &reader)
{
    Witness witness;
    witness.a = read_text_vector<Scalar>(reader);
    witness.b = read_text_vector<Scalar>(reader);
    witness.c = read_text_vector<Scalar>(reader);
    witness.public_input = read_text_vector<Scalar>(reader);
    return witness;
}

void write_srs_data(Writer &writer, const UniversalSrs &srs)
{
    write_text_vector(writer, srs.g1_powers);
    writer.write_text(srs.g2);
    writer.write_text(srs.tau_g2);
}

UniversalSrs read_srs_data(Reader &reader)
{
    UniversalSrs srs;
    srs.g1_powers = read_text_vector<G1>(reader);
    srs.g2 = reader.read_text<G2>();
    srs.tau_g2 = reader.read_text<G2>();
    return srs;
}

void write_domain(Writer &writer, const DomainParameters &domain)
{
    writer.write_u64(domain.domain_size);
    writer.write_u64(domain.quotient_domain_size);
    writer.write_text(domain.omega);
    writer.write_text(domain.k_1);
    writer.write_text(domain.k_2);
    writer.write_text(domain.coset_shift);
}

DomainParameters read_domain(Reader &reader)
{
    DomainParameters domain;
    domain.domain_size = reader.read_size();
    domain.quotient_domain_size = reader.read_size();
    domain.omega = reader.read_text<Scalar>();
    domain.k_1 = reader.read_text<Scalar>();
    domain.k_2 = reader.read_text<Scalar>();
    domain.coset_shift = reader.read_text<Scalar>();
    return domain;
}

void write_polynomial(Writer &writer, const PolynomialData &polynomial)
{
    write_text_vector(writer, polynomial.evaluations);
    write_text_vector(writer, polynomial.coefficients);
    write_text_vector(writer, polynomial.coset_evaluations);
}

PolynomialData read_polynomial(Reader &reader)
{
    return {read_text_vector<Scalar>(reader),
            read_text_vector<Scalar>(reader),
            read_text_vector<Scalar>(reader)};
}

void write_fixed(Writer &writer, const FixedPolynomials &fixed)
{
    write_polynomial(writer, fixed.q_l);
    write_polynomial(writer, fixed.q_r);
    write_polynomial(writer, fixed.q_m);
    write_polynomial(writer, fixed.q_o);
    write_polynomial(writer, fixed.q_c);
    write_polynomial(writer, fixed.sigma_1);
    write_polynomial(writer, fixed.sigma_2);
    write_polynomial(writer, fixed.sigma_3);
}

FixedPolynomials read_fixed(Reader &reader)
{
    FixedPolynomials fixed;
    fixed.q_l = read_polynomial(reader);
    fixed.q_r = read_polynomial(reader);
    fixed.q_m = read_polynomial(reader);
    fixed.q_o = read_polynomial(reader);
    fixed.q_c = read_polynomial(reader);
    fixed.sigma_1 = read_polynomial(reader);
    fixed.sigma_2 = read_polynomial(reader);
    fixed.sigma_3 = read_polynomial(reader);
    return fixed;
}

void write_commitments(Writer &writer, const CircuitCommitments &commitments)
{
    writer.write_text(commitments.q_l);
    writer.write_text(commitments.q_r);
    writer.write_text(commitments.q_m);
    writer.write_text(commitments.q_o);
    writer.write_text(commitments.q_c);
    writer.write_text(commitments.sigma_1);
    writer.write_text(commitments.sigma_2);
    writer.write_text(commitments.sigma_3);
}

CircuitCommitments read_commitments(Reader &reader)
{
    CircuitCommitments commitments;
    commitments.q_l = reader.read_text<G1>();
    commitments.q_r = reader.read_text<G1>();
    commitments.q_m = reader.read_text<G1>();
    commitments.q_o = reader.read_text<G1>();
    commitments.q_c = reader.read_text<G1>();
    commitments.sigma_1 = reader.read_text<G1>();
    commitments.sigma_2 = reader.read_text<G1>();
    commitments.sigma_3 = reader.read_text<G1>();
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
    Reader reader(path, "circuit");
    Circuit value = read_circuit_data(reader);
    reader.finish();
    return value;
}

void write_circuit(const std::filesystem::path &path, const Circuit &circuit)
{
    Writer writer(path, "circuit");
    write_circuit_data(writer, circuit);
}

Witness read_witness(const std::filesystem::path &path)
{
    Reader reader(path, "witness");
    Witness value = read_witness_data(reader);
    reader.finish();
    return value;
}

void write_witness(const std::filesystem::path &path, const Witness &witness)
{
    Writer writer(path, "witness");
    write_witness_data(writer, witness);
}

UniversalSrs read_srs(const std::filesystem::path &path)
{
    Reader reader(path, "srs");
    UniversalSrs value = read_srs_data(reader);
    reader.finish();
    return value;
}

void write_srs(const std::filesystem::path &path, const UniversalSrs &srs)
{
    Writer writer(path, "srs");
    write_srs_data(writer, srs);
}

ProvingKey read_proving_key(const std::filesystem::path &path)
{
    Reader reader(path, "proving-key");
    ProvingKey key;
    key.circuit = read_circuit_data(reader);
    key.domain = read_domain(reader);
    key.fixed = read_fixed(reader);
    key.commitments = read_commitments(reader);
    key.srs = read_srs_data(reader);
    reader.finish();
    return key;
}

void write_proving_key(const std::filesystem::path &path, const ProvingKey &key)
{
    Writer writer(path, "proving-key");
    write_circuit_data(writer, key.circuit);
    write_domain(writer, key.domain);
    write_fixed(writer, key.fixed);
    write_commitments(writer, key.commitments);
    write_srs_data(writer, key.srs);
}

VerificationKey read_verification_key(const std::filesystem::path &path)
{
    Reader reader(path, "verification-key");
    VerificationKey key;
    key.domain = read_domain(reader);
    key.commitments = read_commitments(reader);
    key.g2 = reader.read_text<G2>();
    key.tau_g2 = reader.read_text<G2>();
    reader.finish();
    return key;
}

void write_verification_key(const std::filesystem::path &path, const VerificationKey &key)
{
    Writer writer(path, "verification-key");
    write_domain(writer, key.domain);
    write_commitments(writer, key.commitments);
    writer.write_text(key.g2);
    writer.write_text(key.tau_g2);
}

Proof read_proof(const std::filesystem::path &path)
{
    Reader reader(path, "proof");
    Proof proof{read_bytes(reader)};
    reader.finish();
    return proof;
}

void write_proof(const std::filesystem::path &path, const Proof &proof)
{
    Writer writer(path, "proof");
    write_bytes(writer, proof.encoded);
}

} // namespace nkr::plonk
