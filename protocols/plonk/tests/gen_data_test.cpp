#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "input_data.hpp"
#include "plonk/io.hpp"
#include "plonk/protocol.hpp"

using namespace nkr::plonk;
using nkr::DataMode;

static void require(const bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

static void test_sampler()
{
    for (const DataMode mode : {DataMode::Dense, DataMode::Sparse, DataMode::Hybrid}) {
        require(nkr::parse_data_mode(nkr::data_mode_name(mode)) == mode, "data mode round trip");
        for (std::size_t count = 0; count <= 65; ++count) {
            const nkr::InputSampler sampler(count, mode);
            std::vector<Scalar> inputs;
            std::size_t calls = 0;
            for (std::size_t index = 0; index < count; ++index) {
                inputs.push_back(sampler.sample<Scalar>(index, [&calls] {
                    return ++calls % 2 == 1 ? Scalar::zero() : Scalar::one();
                }));
            }
            const std::size_t dense_count = mode == DataMode::Hybrid ? count - count / 2 : 0;
            const std::size_t zeros = mode == DataMode::Dense ? 0 : (count - dense_count) / 2;
            require(static_cast<std::size_t>(std::count(inputs.begin(), inputs.end(),
                                                       Scalar::zero())) == zeros,
                    "incorrect sampled zero count");
            require(calls == 2 * (count - zeros), "random zero was not retried");
            require(std::count(inputs.begin(), inputs.begin() + dense_count,
                               Scalar::zero()) == 0, "hybrid prefix is not dense");
        }
    }
    for (const std::string name : {"", "unknown", "Dense"}) {
        bool rejected = false;
        try {
            nkr::parse_data_mode(name);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "invalid mode accepted");
    }
}

static bool same_circuit(const Circuit &left, const Circuit &right)
{
    return left.domain_size == right.domain_size && left.rows == right.rows &&
        left.public_inputs == right.public_inputs && left.q_l == right.q_l &&
        left.q_r == right.q_r && left.q_m == right.q_m && left.q_o == right.q_o &&
        left.q_c == right.q_c && left.sigma_1 == right.sigma_1 &&
        left.sigma_2 == right.sigma_2 && left.sigma_3 == right.sigma_3;
}

static void check_trace(const Circuit &circuit, const Witness &witness)
{
    std::string error;
    require(validate_circuit(circuit, error), "invalid circuit: " + error);
    ProvingKey key;
    key.circuit = circuit;
    key.domain.domain_size = circuit.domain_size;
    require(validate_witness(key, witness, error), "invalid witness: " + error);
    const std::array<const std::vector<Scalar> *, 3> wires{&witness.a, &witness.b, &witness.c};
    const std::array<const std::vector<std::uint32_t> *, 3> sigmas{
        &circuit.sigma_1, &circuit.sigma_2, &circuit.sigma_3};
    for (std::size_t column = 0; column < wires.size(); ++column) {
        for (std::size_t row = 0; row < circuit.domain_size; ++row) {
            const std::size_t target = sigmas[column]->at(row);
            require(wires[column]->at(row) ==
                    wires[target / circuit.domain_size]->at(target % circuit.domain_size),
                    "copy-permutation constraint violated");
        }
    }
}

static std::vector<Scalar> check_inputs(const std::string &case_name, const std::size_t size,
                                       const Circuit &circuit, const Witness &witness)
{
    std::vector<Scalar> inputs;
    if (case_name == "matmul") {
        const std::size_t square = size * size;
        require(circuit.public_inputs == 3 * square, "incorrect matmul public inputs");
        for (std::size_t index = 0; index < square; ++index) {
            inputs.push_back(witness.public_input.at(index));
            inputs.push_back(witness.public_input.at(square + index));
        }
        for (std::size_t row = 0; row < size; ++row) {
            for (std::size_t column = 0; column < size; ++column) {
                Scalar sum = Scalar::zero();
                for (std::size_t inner = 0; inner < size; ++inner)
                    sum += witness.public_input.at(row * size + inner) *
                           witness.public_input.at(square + inner * size + column);
                require(witness.public_input.at(2 * square + row * size + column) == sum,
                        "incorrect matrix product");
            }
        }
    } else {
        require(circuit.public_inputs == 3, "incorrect fibonacci public inputs");
        inputs = {witness.public_input.at(0), witness.public_input.at(1)};
        Scalar previous = inputs[0];
        Scalar current = inputs[1];
        for (std::size_t step = 2; step <= size; ++step) {
            const Scalar next = previous + current;
            previous = current;
            current = next;
        }
        require(witness.public_input.at(2) == current, "incorrect fibonacci result");
    }
    return inputs;
}

static void check_artifacts(const std::filesystem::path &directory)
{
    for (const std::string case_name : {"matmul", "fibonacci"}) {
        for (const std::size_t size : {case_name == "matmul" ? 1U : 2U, 4U}) {
            const std::string base = case_name + "_" + std::to_string(size);
            const auto dense = read_circuit(artifact_paths(directory, base).circuit);
            for (const DataMode mode : {DataMode::Dense, DataMode::Sparse, DataMode::Hybrid}) {
                std::string stem = base;
                if (mode != DataMode::Dense) stem += "_" + std::string(nkr::data_mode_name(mode));
                const auto paths = artifact_paths(directory, stem);
                const auto circuit = read_circuit(paths.circuit);
                const auto witness = read_witness(paths.witness);
                require(same_circuit(circuit, dense), stem + ": circuit structure changed");
                check_trace(circuit, witness);
                const auto inputs = check_inputs(case_name, size, circuit, witness);
                const std::size_t expected_zeros = mode == DataMode::Dense ? 0 :
                    inputs.size() / (mode == DataMode::Sparse ? 2 : 4);
                require(static_cast<std::size_t>(std::count(inputs.begin(), inputs.end(),
                                                           Scalar::zero())) == expected_zeros,
                        stem + ": incorrect input zero count");
                if (mode == DataMode::Hybrid)
                    require(std::count(inputs.begin(), inputs.begin() + inputs.size() / 2,
                                       Scalar::zero()) == 0, stem + ": hybrid prefix is not dense");
                std::cout << "PASS: " << stem << " inputs, results, gates, and copy constraints\n";
            }
        }
    }
}

int main(int argc, char **argv) try
{
    Curve::init_public_params();
    test_sampler();
    if (argc == 2) check_artifacts(argv[1]);
    else require(argc == 1, "usage: plonk-gen-data-test [artifact-directory]");
    return 0;
} catch (const std::exception &error) {
    std::cerr << "gen-data-test: " << error.what() << '\n';
    return 1;
}
