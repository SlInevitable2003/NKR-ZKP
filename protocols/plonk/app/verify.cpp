#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

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
              << " [--path <dir>] [--profile] <circuit-file>\n";
    std::exit(2);
}

} // namespace

int main(int argc, char **argv)
{
    std::filesystem::path directory = APP_DATA_DIR;
    std::string filename;
    bool profile = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--path" && i + 1 < argc) directory = argv[++i];
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
        ProfileScope total("plonk.verify.total");
        const ArtifactPaths paths = artifact_paths(directory, filename);
        VerificationKey verification_key;
        Witness witness;
        Proof proof;
        {
            ProfileScope stage("plonk.verify.read_inputs");
            verification_key = read_verification_key(paths.verification_key);
            witness = read_witness(paths.witness);
            proof = read_proof(paths.proof);
        }
        bool ok;
        {
            ProfileScope stage("plonk.verify.pairing_check");
            ok = verify(verification_key, witness.public_input, proof);
        }
        std::cout << (ok ? "Proof verification succeeded."
                         : "Proof verification FAILED.")
                  << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "plonk-verify: " << error.what() << '\n';
        return 1;
    }
}
