#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "groth16/backend.hpp"
#include "groth16/io.hpp"
#include "groth16/protocol.hpp"

using namespace nkr::groth16;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static void usage(const char *program)
{
    std::cerr << "Usage: " << program << " [--backend cpu|cuda|dcu] [--path <dir>] <r1cs-file>\n";
    std::exit(2);
}

int main(int argc, char **argv)
{
    Curve::init_public_params();
    std::filesystem::path directory = APP_DATA_DIR;
    std::string backend_name = "cpu", filename;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--backend" && i + 1 < argc) backend_name = argv[++i];
        else if (argument == "--path" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--help" || argument == "-h") usage(argv[0]);
        else if (!argument.empty() && argument.front() == '-') usage(argv[0]);
        else filename = argument;
    }
    if (filename.empty()) usage(argv[0]);

    try {
        const auto paths = artifact_paths(directory, filename);
        const Circuit circuit = read_circuit(paths.r1cs);
        std::string validation_error;
        if (!validate_circuit(circuit, validation_error))
            throw std::runtime_error("invalid circuit: " + validation_error);
        auto backend = make_backend(backend_name);
        std::cout << "Groth16 setup backend: " << backend->name() << '\n';
        ProvingKey proving_key; VerificationKey verification_key;
        backend->setup(circuit, proving_key, verification_key);
        write_proving_key(paths.proving_key, proving_key);
        write_verification_key(paths.verification_key, verification_key);
        std::cout << "Wrote " << paths.proving_key << " and " << paths.verification_key << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "setup: " << error.what() << '\n';
        return 1;
    }
}

