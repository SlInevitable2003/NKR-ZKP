#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "groth16/io.hpp"
#include "groth16/profile.hpp"
#include "groth16/protocol.hpp"

using namespace nkr::groth16;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static void usage(const char *program)
{
    std::cerr << "Usage: " << program << " [--path <dir>] [--profile] <r1cs-file>\n";
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
        ProfileScope total("groth16.verify.total");
        const auto paths = artifact_paths(directory, filename);
        Circuit circuit;
        std::vector<Scalar> witness;
        VerificationKey key;
        Proof proof;
        {
            ProfileScope stage("groth16.verify.read_inputs");
            circuit = read_circuit(paths.r1cs);
            witness = read_witness(paths.witness);
            key = read_verification_key(paths.verification_key);
            proof = read_proof(paths.proof);
        }
        if (witness.size() < circuit.public_inputs + 1) {
            std::cerr << "Witness is too short for the public inputs.\n";
            return 1;
        }
        const std::vector<Scalar> public_input(
            witness.begin() + 1, witness.begin() + 1 + circuit.public_inputs);
        bool ok;
        {
            ProfileScope stage("groth16.verify.pairing_check");
            ok = verify(key, public_input, proof);
        }
        std::cout << (ok ? "Proof verification succeeded." : "Proof verification FAILED.") << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "verify: " << error.what() << '\n';
        return 1;
    }
}
