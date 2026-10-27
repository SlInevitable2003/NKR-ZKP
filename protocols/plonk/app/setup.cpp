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
              << " [--backend cpu|cuda|hip] [--path <dir>] [--srs <file>]"
                 " [--profile] <circuit-file>\n"
              << "  Without --srs, setup generates and writes <stem>.srs.bin.\n";
    std::exit(2);
}

UniversalSrs generate_srs(const std::size_t max_degree)
{
    Scalar tau;
    do {
        tau = Scalar::random_element();
    } while (tau == Scalar::zero() || tau == Scalar::one());

    UniversalSrs srs;
    srs.g1_powers.resize(max_degree + 1);
    Scalar power = Scalar::one();
    for (G1 &point : srs.g1_powers) {
        point = power * G1::one();
        power *= tau;
    }
    srs.g2 = G2::one();
    srs.tau_g2 = tau * srs.g2;
    return srs;
}

} // namespace

int main(int argc, char **argv)
{
    std::filesystem::path directory = APP_DATA_DIR;
    std::filesystem::path supplied_srs;
    std::string backend_name = "cpu";
    std::string filename;
    bool profile = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--backend" && i + 1 < argc) backend_name = argv[++i];
        else if (argument == "--path" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--srs" && i + 1 < argc) supplied_srs = argv[++i];
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
        ProfileScope total("plonk.setup.total");
        const ArtifactPaths paths = artifact_paths(directory, filename);

        Circuit circuit;
        {
            ProfileScope stage("plonk.setup.read_circuit");
            circuit = read_circuit(paths.circuit);
        }
        {
            ProfileScope stage("plonk.setup.validate_circuit");
            std::string validation_error;
            if (!validate_circuit(circuit, validation_error))
                throw std::runtime_error("invalid circuit: " + validation_error);
        }

        UniversalSrs srs;
        const bool generated_srs = supplied_srs.empty();
        std::filesystem::path srs_path = supplied_srs;
        if (!srs_path.empty() && srs_path.is_relative()) srs_path = directory / srs_path;
        {
            ProfileScope stage("plonk.setup.prepare_srs");
            if (generated_srs) {
                srs = generate_srs(circuit.domain_size + 2);
                srs_path = paths.srs;
            } else {
                srs = read_srs(srs_path);
            }
        }

        auto backend = make_backend(backend_name);
        std::cout << "PLONK setup backend: " << backend->name() << '\n';
        ProvingKey proving_key;
        VerificationKey verification_key;
        {
            ProfileScope stage("plonk.setup.backend");
            setup(*backend, circuit, srs, proving_key, verification_key);
        }

        std::filesystem::create_directories(directory);
        {
            ProfileScope stage("plonk.setup.write_artifacts");
            if (generated_srs) write_srs(srs_path, srs);
            write_proving_key(paths.proving_key, proving_key);
            write_verification_key(paths.verification_key, verification_key);
        }
        if (generated_srs) std::cout << "Wrote SRS to " << srs_path << '\n';
        std::cout << "Wrote " << paths.proving_key << " and "
                  << paths.verification_key << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "plonk-setup: " << error.what() << '\n';
        return 1;
    }
}
