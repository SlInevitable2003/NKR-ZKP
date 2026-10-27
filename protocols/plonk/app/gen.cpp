#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "plonk/arithmetization.hpp"
#include "plonk/io.hpp"
#include "plonk/profile.hpp"

using namespace nkr::plonk;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

namespace {

[[noreturn]] void usage(const char *program)
{
    std::cerr << "Usage: " << program << " [--path <dir>] [--profile] <case> [params...]\n"
              << "  cases:\n"
              << "    matmul <size>       matrix multiplication of dimension <size>\n"
              << "    fibonacci <steps>   Fibonacci recurrence with <steps> >= 2\n"
              << "  Outputs:\n"
              << "    <stem>.bin          PLONK circuit\n"
              << "    <stem>.witness.bin  satisfying execution trace\n";
    std::exit(2);
}

std::size_t parse_size(const std::string &text)
{
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(text, &consumed);
    if (consumed != text.size() || parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("invalid positive size: " + text);
    }
    return static_cast<std::size_t>(parsed);
}

std::size_t checked_product(const std::size_t lhs, const std::size_t rhs)
{
    if (rhs != 0 && lhs > std::numeric_limits<std::size_t>::max() / rhs)
        throw std::overflow_error("circuit size overflows size_t");
    return lhs * rhs;
}

void build_matmul(PlonkishManager &manager, const std::size_t size)
{
    const std::size_t square = checked_product(size, size);
    const std::size_t cube = checked_product(square, size);
    if (square > Variable::invalid_index / 3 ||
        cube > Variable::invalid_index - 3 * square) {
        throw std::overflow_error("matrix circuit exceeds the PLONK variable index space");
    }

    std::vector<Scalar> a_values(square);
    std::vector<Scalar> b_values(square);
    for (std::size_t i = 0; i < square; ++i) {
        a_values[i] = Scalar::random_element();
        b_values[i] = Scalar::random_element();
    }

    std::vector<Scalar> product_values(cube);
    std::vector<Scalar> c_values(square, Scalar::zero());
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            for (std::size_t inner = 0; inner < size; ++inner) {
                const std::size_t product_index = row * square + column * size + inner;
                product_values[product_index] =
                    a_values[row * size + inner] * b_values[inner * size + column];
                c_values[row * size + column] += product_values[product_index];
            }
        }
    }

    std::vector<Variable> a(square), b(square), c(square), products(cube);
    for (std::size_t i = 0; i < square; ++i) a[i] = manager.public_variable(a_values[i]);
    for (std::size_t i = 0; i < square; ++i) b[i] = manager.public_variable(b_values[i]);
    for (std::size_t i = 0; i < square; ++i) c[i] = manager.public_variable(c_values[i]);
    for (std::size_t i = 0; i < cube; ++i)
        products[i] = manager.private_variable(product_values[i]);

    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            std::vector<LinearTerm> sum_terms;
            sum_terms.reserve(size);
            for (std::size_t inner = 0; inner < size; ++inner) {
                const Variable product = products[row * square + column * size + inner];
                manager.enforce_multiplication(
                    product, a[row * size + inner], b[inner * size + column]);
                sum_terms.push_back({Scalar::one(), product});
            }
            const Variable sum = manager.linear_combination(sum_terms);
            manager.assert_equal(c[row * size + column], sum);
        }
    }
}

void build_fibonacci(PlonkishManager &manager, const std::size_t steps)
{
    if (steps < 2) throw std::invalid_argument("fibonacci steps must be at least 2");
    const Scalar first_value = Scalar::random_element();
    const Scalar second_value = Scalar::random_element();
    Scalar previous_value = first_value;
    Scalar current_value = second_value;
    for (std::size_t step = 2; step <= steps; ++step) {
        const Scalar next = previous_value + current_value;
        previous_value = current_value;
        current_value = next;
    }

    Variable previous = manager.public_variable(first_value);
    Variable current = manager.public_variable(second_value);
    const Variable expected = manager.public_variable(current_value);
    for (std::size_t step = 2; step <= steps; ++step) {
        const Variable next = manager.add(previous, current);
        previous = current;
        current = next;
    }
    manager.assert_equal(current, expected);
}

} // namespace

int main(int argc, char **argv)
{
    std::filesystem::path directory = APP_DATA_DIR;
    bool profile = false;
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--path" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--profile") profile = true;
        else if (argument == "--help" || argument == "-h") usage(argv[0]);
        else if (!argument.empty() && argument.front() == '-') usage(argv[0]);
        else arguments.push_back(argument);
    }
    if (arguments.size() != 2) usage(argv[0]);

    try {
        configure_profiling(profile);
        Curve::init_public_params();
        ProfileScope total("plonk.gen.total");
        const std::string case_name = arguments[0];
        const std::size_t parameter = parse_size(arguments[1]);

        PlonkishManager manager;
        std::string stem;
        {
            ProfileScope stage("plonk.gen.build_circuit");
            if (case_name == "matmul") {
                build_matmul(manager, parameter);
                stem = "matmul_" + std::to_string(parameter);
            } else if (case_name == "fibonacci") {
                build_fibonacci(manager, parameter);
                stem = "fibonacci_" + std::to_string(parameter);
            } else {
                throw std::invalid_argument("unknown circuit case: " + case_name);
            }
        }

        Arithmetization output;
        {
            ProfileScope stage("plonk.gen.finalize");
            output = manager.finalize();
        }
        std::filesystem::create_directories(directory);
        const ArtifactPaths paths = artifact_paths(directory, stem);
        {
            ProfileScope stage("plonk.gen.write_artifacts");
            write_circuit(paths.circuit, output.circuit);
            write_witness(paths.witness, output.witness);
        }

        std::cout << "* PLONK rows:          " << output.circuit.rows << '\n'
                  << "* PLONK domain size:   " << output.circuit.domain_size << '\n'
                  << "* PLONK public inputs: " << output.circuit.public_inputs << '\n'
                  << "Wrote circuit to " << paths.circuit << '\n'
                  << "Wrote witness to " << paths.witness << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "plonk-gen: " << error.what() << '\n';
        return 1;
    }
}
