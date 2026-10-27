#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "plonk/backend.hpp"
#include "plonk/io.hpp"
#include "plonk/profile.hpp"
#include "plonk/protocol.hpp"

using namespace nkr::plonk;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

namespace {

[[noreturn]] void usage(const char *program)
{
    std::cerr << "Usage: " << program
              << " [--backend cpu|cuda|hip] [--path <dir>] [--profile] <circuit-file>\n";
    std::exit(2);
}

} // namespace

int main(int argc, char **argv)
{
    std::filesystem::path directory = APP_DATA_DIR;
    std::string backend_name = "cpu";
    std::string filename;
    bool profile = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--backend" && i + 1 < argc) backend_name = argv[++i];
        else if (argument == "--path" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--profile") profile = true;
        else if (argument == "--help" || argument == "-h") usage(argv[0]);
        else if (!argument.empty() && argument.front() == '-') usage(argv[0]);
        else if (filename.empty()) filename = argument;
        else usage(argv[0]);
    }
    if (filename.empty()) usage(argv[0]);

    try {
        configure_profiling(profile);
        Curve::init_public_params();
        ProfileScope total("plonk.prove.total");
        const ArtifactPaths paths = artifact_paths(directory, filename);
        ProvingKey proving_key;
        Witness witness;
        {
            ProfileScope stage("plonk.prove.read_inputs");
            proving_key = read_proving_key(paths.proving_key);
            witness = read_witness(paths.witness);
        }
        std::string validation_error;
        {
            ProfileScope stage("plonk.prove.validate_inputs");
            if (!validate_proving_key(proving_key, validation_error))
                throw std::runtime_error("invalid proving key: " + validation_error);
            if (!validate_witness(proving_key, witness, validation_error))
                throw std::runtime_error("invalid witness: " + validation_error);
        }
        auto backend = make_backend(backend_name);
        std::cout << "PLONK prove backend: " << backend->name() << '\n';
        Proof proof;
        {
            ProfileScope stage("plonk.prove.backend");
            proof = prove(*backend, proving_key, witness);
        }
        {
            ProfileScope stage("plonk.prove.write_proof");
            write_proof(paths.proof, proof);
        }
        std::cout << "Wrote " << paths.proof << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "plonk-prove: " << error.what() << '\n';
        return 1;
    }
}
