#include "plonk/arithmetization.hpp"
#include "plonk/backend.hpp"
#include "plonk/io.hpp"
#include "plonk/profile.hpp"
#include "plonk/protocol.hpp"
#include "plonk/transcript.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

using namespace nkr::plonk;

namespace {

UniversalSrs make_test_srs(const std::size_t max_degree, const Scalar &tau)
{
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

int main()
{
    Curve::init_public_params();
    configure_profiling(false);

    Transcript transcript;
    transcript.append_label("hello");
    transcript.append_label("world");
    const Scalar first_expected(libff::bigint<Scalar::num_limbs>(
        "1364633289518614123241436023962023498624305175188411904553360540401925727122"));
    assert(transcript.challenge() == first_expected);
    transcript.append_label("again");
    const Scalar second_expected(libff::bigint<Scalar::num_limbs>(
        "6112880534883560843658091743079907571799864223450289383241386695414524549529"));
    assert(transcript.challenge() == second_expected);

    PlonkishManager manager;
    const Variable x = manager.public_variable(Scalar(3));
    const Variable y = manager.public_variable(Scalar(4));
    const Variable expected = manager.public_variable(Scalar(19));
    const Variable product = manager.multiply(x, y);
    const Variable seven = manager.constant(Scalar(7));
    const Variable result = manager.add(product, seven);
    manager.assert_equal(result, expected);
    const Arithmetization arithmetization = manager.finalize(8);

    const UniversalSrs srs =
        make_test_srs(arithmetization.circuit.domain_size + 2, Scalar(17));
    auto backend = make_cpu_backend();
    ProvingKey proving_key;
    VerificationKey verification_key;
    setup(*backend, arithmetization.circuit, srs, proving_key, verification_key);

    const Proof proof = prove(*backend, proving_key, arithmetization.witness);
    assert(verify(verification_key, arithmetization.witness.public_input, proof));

    Proof changed_scalar = proof;
    changed_scalar.a_zeta += Scalar::one();
    assert(!verify(verification_key, arithmetization.witness.public_input, changed_scalar));

    Proof changed_opening = proof;
    changed_opening.w_zeta = changed_opening.w_zeta + G1::one();
    assert(!verify(verification_key, arithmetization.witness.public_input, changed_opening));

    std::vector<Scalar> changed_public_input = arithmetization.witness.public_input;
    changed_public_input[0] += Scalar::one();
    assert(!verify(verification_key, changed_public_input, proof));

    VerificationKey changed_key = verification_key;
    changed_key.commitments.q_l = changed_key.commitments.q_l + G1::one();
    assert(!verify(changed_key, arithmetization.witness.public_input, proof));

    Witness invalid_witness = arithmetization.witness;
    invalid_witness.a[0] += Scalar::one();
    bool rejected = false;
    try {
        (void)prove(*backend, proving_key, invalid_witness);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert(rejected);

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("nkr-plonk-proof-test-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "proof.bin";
    write_proof(path, proof);
    const Proof loaded = read_proof(path);
    assert(verify(verification_key, arithmetization.witness.public_input, loaded));
    std::filesystem::remove_all(directory);
    return 0;
}
