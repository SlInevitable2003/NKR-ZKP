#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "plonk/io.hpp"
#include "profile.hpp"
#include "plonk/protocol.hpp"

using namespace nkr::plonk;
using nkr::ProfileScope;
using nkr::configure_profiling;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static void usage(const char *program)
{
    std::cerr << "Usage: " << program << " [--path <dir>] [--profile] <circuit-file>\n";
    std::exit(2);
}

int main(int argc, char **argv)
{
    std::filesystem::path directory = APP_DATA_DIR;
    std::string filename;
    bool profile_enabled = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--path" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--profile") profile_enabled = true;
        else if (argument == "--help" || argument == "-h") usage(argv[0]);
        else if (!argument.empty() && argument.front() == '-') usage(argv[0]);
        else filename = argument;
    }
    if (filename.empty()) usage(argv[0]);
    configure_profiling(profile_enabled);
    Curve::init_public_params();

    try {
        ProfileScope total("plonk.verify.total");
        const auto paths = artifact_paths(directory, filename);
        Witness witness;
        VerificationKey key;
        Proof proof;
        {
            ProfileScope stage("plonk.verify.read_inputs");
            witness = read_witness(paths.witness);
            key = read_verification_key(paths.verification_key);
            proof = read_proof(paths.proof);
        }
        bool ok;
        {
            ProfileScope stage("plonk.verify.pairing_check");
            ok = verify(key, witness.public_input, proof);
        }
        std::cout << (ok ? "Proof verification succeeded." : "Proof verification FAILED.") << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "verify: " << error.what() << '\n';
        return 1;
    }
}
