#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "input_data.hpp"
#include "plonk/io.hpp"
#include "plonkish/arithmetization.hpp"
#include "profile.hpp"

using namespace std;
using namespace Plonkish;
using Scalar = nkr::plonk::Scalar;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static size_t parse_size(const string &text)
{
    if (text.empty()) throw invalid_argument("invalid positive size: " + text);
    for (const char character : text) {
        if (character < '0' || character > '9')
            throw invalid_argument("invalid positive size: " + text);
    }
    size_t consumed = 0;
    const unsigned long long parsed = stoull(text, &consumed);
    if (consumed != text.size() || parsed == 0 ||
        parsed > numeric_limits<size_t>::max()) {
        throw invalid_argument("invalid positive size: " + text);
    }
    return static_cast<size_t>(parsed);
}

static size_t checked_product(const size_t lhs, const size_t rhs)
{
    if (rhs != 0 && lhs > numeric_limits<size_t>::max() / rhs)
        throw overflow_error("circuit size overflows size_t");
    return lhs * rhs;
}

static void build_matmul(PlonkishManager &mgr, const size_t s, const nkr::DataMode mode)
{
    const size_t square = checked_product(s, s);
    const size_t cube = checked_product(square, s);
    if (square > Variable::invalid_index / 3 ||
        cube > Variable::invalid_index - 3 * square) {
        throw overflow_error("matrix circuit exceeds the PLONK variable index space");
    }

    vector<Scalar> av(square), bv(square);
    const nkr::InputSampler sampler(checked_product(2, square), mode);
    #pragma omp parallel for
    for (size_t i = 0; i < square; ++i) {
        av[i] = sampler.sample<Scalar>(2 * i, [] { return Scalar::random_element(); });
        bv[i] = sampler.sample<Scalar>(2 * i + 1, [] { return Scalar::random_element(); });
    }

    vector<Scalar> cv(square, Scalar::zero()), pv(cube);
    for (size_t i = 0; i < s; ++i) {
        for (size_t j = 0; j < s; ++j) {
            for (size_t l = 0; l < s; ++l) {
                const size_t index = i * square + j * s + l;
                pv[index] = av[i * s + l] * bv[l * s + j];
                cv[i * s + j] += pv[index];
            }
        }
    }

    vector<Variable> A(square), B(square), C(square), P(cube);
    for (size_t i = 0; i < square; ++i) A[i] = mgr.public_variable(av[i]);
    for (size_t i = 0; i < square; ++i) B[i] = mgr.public_variable(bv[i]);
    for (size_t i = 0; i < square; ++i) C[i] = mgr.public_variable(cv[i]);
    for (size_t i = 0; i < cube; ++i) P[i] = mgr.private_variable(pv[i]);

    for (size_t i = 0; i < s; ++i) {
        for (size_t j = 0; j < s; ++j) {
            vector<LinearTerm> sum_terms;
            sum_terms.reserve(s);
            for (size_t l = 0; l < s; ++l) {
                const Variable product = P[i * square + j * s + l];
                mgr.enforce_multiplication(product, A[i * s + l], B[l * s + j]);
                sum_terms.push_back({Scalar::one(), product});
            }
            const Variable sum = mgr.linear_combination(sum_terms);
            mgr.assert_equal(C[i * s + j], sum);
        }
    }
}

static void build_fibonacci(PlonkishManager &mgr, const size_t steps, const nkr::DataMode mode)
{
    if (steps < 2) throw invalid_argument("fibonacci steps must be at least 2");
    if (steps > Variable::invalid_index - 2)
        throw overflow_error("fibonacci circuit exceeds the PLONK variable index space");
    const nkr::InputSampler sampler(2, mode);
    const Scalar first_value = sampler.sample<Scalar>(0, [] { return Scalar::random_element(); });
    const Scalar second_value = sampler.sample<Scalar>(1, [] { return Scalar::random_element(); });
    Scalar previous_value = first_value;
    Scalar current_value = second_value;
    for (size_t step = 2; step <= steps; ++step) {
        const Scalar next = previous_value + current_value;
        previous_value = current_value;
        current_value = next;
    }

    Variable previous = mgr.public_variable(first_value);
    Variable current = mgr.public_variable(second_value);
    const Variable expected = mgr.public_variable(current_value);
    for (size_t step = 2; step <= steps; ++step) {
        const Variable next = mgr.add(previous, current);
        previous = current;
        current = next;
    }
    mgr.assert_equal(current, expected);
}

static void usage(const char *prog) {
    cerr << "Usage: " << prog
         << " [--profile] [--data-mode dense|sparse|hybrid] <case> [params...]\n"
         << "  cases:\n"
         << "    matmul <size>      matrix multiplication of dimension <size>\n"
         << "    fibonacci <steps>  Fibonacci recurrence with <steps> >= 2\n"
         << "  data modes (original inputs only, not derived trace values):\n"
         << "    dense (default)    all inputs are random nonzero values\n"
         << "    sparse             half of the inputs are zero, at shuffled positions\n"
         << "    hybrid             first half dense, second half sparse\n"
         << "                       zero counts round down for odd-sized regions\n"
         << "                       Fibonacci has only two seeds: hybrid has no zeros\n"
         << "  Outputs (in " APP_DATA_DIR "):\n"
         << "    sparse/hybrid append _<mode> to the default case/size stem\n"
         << "    <stem>.bin         PLONK circuit\n"
         << "    <stem>.witness.bin satisfying execution trace\n";
    exit(1);
}

int main(int argc, char *argv[]) try {
    bool profile = false;
    nkr::DataMode mode = nkr::DataMode::Dense;
    vector<string> arguments;
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "--profile") profile = true;
        else if (string(argv[i]) == "--data-mode") {
            if (i + 1 == argc) throw invalid_argument("--data-mode requires a value");
            mode = nkr::parse_data_mode(argv[++i]);
        }
        else if (string(argv[i]).rfind("-", 0) == 0) usage(argv[0]);
        else arguments.emplace_back(argv[i]);
    }
    if (arguments.empty()) usage(argv[0]);

    nkr::configure_profiling(profile);
    nkr::plonk::Curve::init_public_params();
    nkr::ProfileScope total("plonk.gen.total");
    const string case_name = arguments[0];

    PlonkishManager mgr;
    string stem;

    if (case_name == "matmul") {
        if (arguments.size() != 2) usage(argv[0]);
        const size_t s = parse_size(arguments[1]);
        {
            nkr::ProfileScope stage("plonk.gen.build_matmul");
            build_matmul(mgr, s, mode);
        }
        stem = "matmul_" + to_string(s);
    } else if (case_name == "fibonacci") {
        if (arguments.size() != 2) usage(argv[0]);
        const size_t steps = parse_size(arguments[1]);
        {
            nkr::ProfileScope stage("plonk.gen.build_fibonacci");
            build_fibonacci(mgr, steps, mode);
        }
        stem = "fibonacci_" + to_string(steps);
    } else {
        cerr << "Unknown case: " << case_name << "\n";
        usage(argv[0]);
    }
    if (mode != nkr::DataMode::Dense)
        stem += "_" + string(nkr::data_mode_name(mode));
    cout << "Data mode: " << nkr::data_mode_name(mode) << '\n';

    Arithmetization out;
    {
        nkr::ProfileScope stage("plonk.gen.finalize_circuit");
        out = mgr.finalize();
    }

    printf("* PLONK rows:          %zu\n", out.circuit.rows);
    printf("* PLONK domain size:   %zu\n", out.circuit.domain_size);
    printf("* PLONK public inputs: %zu\n", out.circuit.public_inputs);

    const string data_dir = APP_DATA_DIR;
    std::filesystem::create_directories(data_dir);
    const auto paths = nkr::plonk::artifact_paths(std::filesystem::path(data_dir), stem);

    {
        nkr::ProfileScope stage("plonk.gen.write_circuit");
        nkr::plonk::write_circuit(paths.circuit, out.circuit);
    }
    cout << "Wrote circuit to " << paths.circuit << endl;

    {
        nkr::ProfileScope stage("plonk.gen.write_witness");
        nkr::plonk::write_witness(paths.witness, out.witness);
    }
    cout << "Wrote witness to " << paths.witness << endl;

    return 0;
} catch (const exception &error) {
    cerr << "gen: " << error.what() << '\n';
    return 1;
}
