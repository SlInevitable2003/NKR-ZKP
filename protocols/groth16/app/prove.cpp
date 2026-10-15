#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "groth16/backend.hpp"
#include "groth16/io.hpp"
#include "groth16/profile.hpp"
#include "groth16/protocol.hpp"

using namespace nkr::groth16;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static void usage(const char *program)
{
    std::cerr << "Usage: " << program
              << " [--backend cpu|cuda|dcu] [--path <dir>] [--profile] <r1cs-file>\n";
    std::exit(2);
}

int main(int argc, char **argv)
{
    std::filesystem::path directory = APP_DATA_DIR;
    std::string backend_name = "cpu", filename;
    bool profile = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--backend" && i + 1 < argc) backend_name = argv[++i];
        else if (argument == "--path" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--profile") profile = true;
        else if (argument == "--help" || argument == "-h") usage(argv[0]);
        else if (!argument.empty() && argument.front() == '-') usage(argv[0]);
        else filename = argument;
    }
    if (filename.empty()) usage(argv[0]);
    configure_profiling(profile);
    Curve::init_public_params();

    try {
        ProfileScope total("groth16.prove.total");
        const auto paths = artifact_paths(directory, filename);
        Circuit circuit;
        std::vector<Scalar> witness;
        ProvingKey proving_key;
        {
            ProfileScope stage("groth16.prove.read_inputs");
            circuit = read_circuit(paths.r1cs);
            witness = read_witness(paths.witness);
            proving_key = read_proving_key(paths.proving_key);
        }
        std::string validation_error;
        {
            ProfileScope stage("groth16.prove.validate_inputs");
            if (!validate_circuit(circuit, validation_error))
                throw std::runtime_error("invalid circuit: " + validation_error);
            if (!validate_proving_key(circuit, proving_key, validation_error))
                throw std::runtime_error("invalid proving key: " + validation_error);
            if (!check_witness(circuit, witness))
                throw std::runtime_error("witness does not satisfy the R1CS");
        }
        auto backend = make_backend(backend_name);
        std::cout << "Groth16 prove backend: " << backend->name() << '\n';
        RawProof raw;
        {
            ProfileScope stage("groth16.prove.backend");
            backend->prove(circuit, proving_key, witness, raw);
        }
        Proof proof;
        {
            ProfileScope stage("groth16.prove.blind_proof");
            proof = blind_proof(proving_key, raw);
        }
        {
            ProfileScope stage("groth16.prove.write_proof");
            write_proof(paths.proof, proof);
        }
        std::cout << "Wrote " << paths.proof << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "prove: " << error.what() << '\n';
        return 1;
    }
}
