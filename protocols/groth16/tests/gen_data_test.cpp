#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "input_data.hpp"
#include "groth16/io.hpp"
#include "groth16/protocol.hpp"
#include "groth16/types.hpp"
#include "r1cs/poseidon.hpp"
#include "r1cs/sql_aggregate.hpp"

using namespace nkr::groth16;
using nkr::DataMode;
using nkr::InputSampler;
using nkr::data_mode_name;
using nkr::parse_data_mode;

static void require(const bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

static void test_sampler()
{
    for (const DataMode mode : {DataMode::Dense, DataMode::Sparse, DataMode::Hybrid}) {
        require(parse_data_mode(data_mode_name(mode)) == mode, "data mode round trip");
        for (std::size_t count = 0; count <= 65; ++count) {
            const InputSampler sampler(count, mode);
            std::vector<Scalar> values;
            std::size_t calls = 0;
            for (std::size_t index = 0; index < count; ++index) {
                values.push_back(sampler.sample<Scalar>(index, [&calls] {
                    return ++calls % 2 == 1 ? Scalar::zero() : Scalar::one();
                }));
            }
            const std::size_t dense_count = mode == DataMode::Hybrid ? count - count / 2 : 0;
            const std::size_t expected_zeros = mode == DataMode::Dense ? 0 :
                (count - dense_count) / 2;
            require(static_cast<std::size_t>(std::count(values.begin(), values.end(),
                                                       Scalar::zero())) == expected_zeros,
                    "incorrect zero count");
            require(calls == 2 * (count - expected_zeros), "zero random values must be retried");
            require(std::count(values.begin(), values.begin() + dense_count,
                               Scalar::zero()) == 0, "hybrid dense prefix contains zeros");
            for (std::size_t index = 0; index < count; ++index) {
                require(sampler.sample<Scalar>(index, [] { return Scalar::one(); }) == values[index],
                        "input mask changes between samples");
            }
        }
    }
    for (const std::string name : {"", "unknown", "Dense"}) {
        bool rejected = false;
        try {
            parse_data_mode(name);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "invalid data mode accepted");
    }
}

static std::vector<std::size_t> input_columns(const std::string &case_name)
{
    using namespace LightZK;
    R1CSManager<Scalar> manager;
    std::vector<std::size_t> ids;
    if (case_name == "matmul") {
        std::vector<Variable<Scalar>> left;
        for (std::size_t index = 0; index < 16; ++index)
            left.emplace_back(VariableType::Public, manager);
        for (std::size_t index = 0; index < 16; ++index) {
            Variable<Scalar> right(VariableType::Public, manager);
            ids.push_back(left[index].get_id());
            ids.push_back(right.get_id());
        }
    } else if (case_name == "poseidon") {
        std::map<std::size_t, Scalar> assignment;
        for (std::size_t index = 0; index < 4; ++index) {
            Variable<Scalar> digest(VariableType::Public, manager);
            Variable<Scalar> left(VariableType::Private, manager);
            Variable<Scalar> right(VariableType::Private, manager);
            ids.push_back(left.get_id());
            ids.push_back(right.get_id());
            Poseidon::add_hash2_constraints(manager, assignment, left, Scalar::zero(),
                                            right, Scalar::zero(), digest);
        }
    } else {
        std::map<std::size_t, Scalar> assignment;
        Variable<Scalar> root(VariableType::Public, manager);
        Variable<Scalar> target(VariableType::Public, manager);
        Variable<Scalar> count(VariableType::Public, manager);
        Variable<Scalar> sum(VariableType::Public, manager);
        const std::vector<SqlAggregate::Row<Scalar>> rows(4, {Scalar::zero(), Scalar::zero()});
        const auto variables = SqlAggregate::add_query_constraints(
            manager, assignment, rows, root, target, Scalar::zero(), count, sum);
        for (std::size_t index = 0; index < rows.size(); ++index) {
            ids.push_back(variables.categories[index].get_id());
            ids.push_back(variables.amounts[index].get_id());
        }
    }
    for (std::size_t &id : ids) id = manager.dense_index(id);
    return ids;
}

static bool same_matrix(const CsrMatrix<Scalar> &left,
                        const CsrMatrix<Scalar> &right)
{
    return left.num_rows == right.num_rows && left.num_cols == right.num_cols &&
        left.row_ptr == right.row_ptr && left.col_idx == right.col_idx && left.values == right.values;
}

static void check_fibonacci_artifacts(const std::filesystem::path &directory)
{
    for (const std::size_t steps : {2, 4, 16}) {
        const std::string dense_stem = "fibonacci_" + std::to_string(steps);
        const auto dense = read_circuit(artifact_paths(directory, dense_stem).r1cs);
        for (const DataMode mode : {DataMode::Dense, DataMode::Sparse, DataMode::Hybrid}) {
            std::string stem = dense_stem;
            if (mode != DataMode::Dense) stem += "_" + std::string(data_mode_name(mode));
            const auto paths = artifact_paths(directory, stem);
            const auto circuit = read_circuit(paths.r1cs);
            const auto witness = read_witness(paths.witness);
            std::string error;
            require(validate_circuit(circuit, error), stem + ": " + error);
            require(circuit.public_inputs == 3, stem + ": expected two public seeds and a result");
            require(check_witness(circuit, witness), stem + ": unsatisfied witness");
            require(same_matrix(circuit.a, dense.a) && same_matrix(circuit.b, dense.b) &&
                    same_matrix(circuit.c, dense.c), stem + ": circuit structure changed");

            const std::size_t zeros = static_cast<std::size_t>(std::count(
                witness.begin() + 1, witness.begin() + 3, Scalar::zero()));
            require(zeros == (mode == DataMode::Sparse ? 1 : 0),
                    stem + ": incorrect seed zero count");
            Scalar previous = witness.at(1);
            Scalar current = witness.at(2);
            for (std::size_t step = 2; step <= steps; ++step) {
                const Scalar next = previous + current;
                require(witness.at(step + 2) == next, stem + ": incorrect recurrence witness");
                previous = current;
                current = next;
            }
            require(witness.at(3) == current, stem + ": incorrect F_steps result");
            for (std::size_t column = 1; column < steps + 3; ++column) {
                auto tampered = witness;
                tampered[column] += Scalar::one();
                require(!check_witness(circuit, tampered),
                        stem + ": tampered seed, result, or intermediate term accepted");
            }
        }
    }
}

static void check_artifacts(const std::filesystem::path &directory)
{
    for (const std::string case_name : {"matmul", "poseidon", "sql_aggregate"}) {
        const auto dense_paths = artifact_paths(directory, case_name + "_4");
        const auto dense = read_circuit(dense_paths.r1cs);
        const auto columns = input_columns(case_name);
        for (const DataMode mode : {DataMode::Dense, DataMode::Sparse, DataMode::Hybrid}) {
            std::string stem = case_name + "_4";
            if (mode != DataMode::Dense) stem += "_" + std::string(data_mode_name(mode));
            const auto paths = artifact_paths(directory, stem);
            const auto circuit = read_circuit(paths.r1cs);
            const auto witness = read_witness(paths.witness);
            std::string error;
            require(validate_circuit(circuit, error), stem + ": " + error);
            require(check_witness(circuit, witness), stem + ": unsatisfied witness");
            require(circuit.public_inputs == dense.public_inputs && same_matrix(circuit.a, dense.a) &&
                    same_matrix(circuit.b, dense.b) && same_matrix(circuit.c, dense.c),
                    stem + ": circuit structure changed");

            std::vector<Scalar> inputs;
            for (const std::size_t column : columns) inputs.push_back(witness.at(column));
            const std::size_t expected_zeros = mode == DataMode::Dense ? 0 :
                inputs.size() / (mode == DataMode::Sparse ? 2 : 4);
            require(static_cast<std::size_t>(std::count(inputs.begin(), inputs.end(),
                                                       Scalar::zero())) == expected_zeros,
                    stem + ": incorrect input zero count");
            if (mode == DataMode::Hybrid)
                require(std::count(inputs.begin(), inputs.begin() + inputs.size() / 2,
                                   Scalar::zero()) == 0, stem + ": hybrid prefix is not dense");
        }
    }
}

int main(int argc, char **argv)
{
    Curve::init_public_params();
    test_sampler();
    if (argc == 3 && std::string(argv[1]) == "--fibonacci") check_fibonacci_artifacts(argv[2]);
    else if (argc == 2) check_artifacts(argv[1]);
    else require(argc == 1, "usage: groth16-gen-data-test [--fibonacci] [artifact-directory]");
    return 0;
}
