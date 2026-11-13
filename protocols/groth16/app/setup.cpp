#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "groth16/backend.hpp"
#include "groth16/io.hpp"
#include "profile.hpp"
#include "groth16/protocol.hpp"

using namespace nkr::groth16;
using nkr::ProfileScope;
using nkr::configure_profiling;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static void usage(const char *program)
{
    std::cerr << "Usage: " << program
              << " [--backend cpu|cuda|hip] [--path <dir>] [--profile] <r1cs-file>\n";
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
        ProfileScope total("groth16.setup.total");
        const auto paths = artifact_paths(directory, filename);
        Circuit circuit;
        {
            ProfileScope stage("groth16.setup.read_circuit");
            circuit = read_circuit(paths.r1cs);
        }
        std::string validation_error;
        {
            ProfileScope stage("groth16.setup.validate_circuit");
            if (!validate_circuit(circuit, validation_error))
                throw std::runtime_error("invalid circuit: " + validation_error);
        }
        auto backend = make_backend(backend_name);
        std::cout << "Groth16 setup backend: " << backend->name() << '\n';
        ProvingKey proving_key; VerificationKey verification_key;
        {
            ProfileScope stage("groth16.setup.backend");
            backend->setup(circuit, proving_key, verification_key);
        }
        {
            ProfileScope stage("groth16.setup.write_keys");
            write_proving_key(paths.proving_key, proving_key);
            write_verification_key(paths.verification_key, verification_key);
        }
        std::cout << "Wrote " << paths.proving_key << " and " << paths.verification_key << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "setup: " << error.what() << '\n';
        return 1;
    }
}
