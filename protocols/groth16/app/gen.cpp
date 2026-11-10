#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "libff/common/profiling.hpp"
#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"

#include "groth16/io.hpp"
#include "input_data.hpp"
#include "profile.hpp"
#include "r1cs/poseidon.hpp"
#include "r1cs/r1cs.hpp"
#include "r1cs/sql_aggregate.hpp"

using namespace std;
using namespace LightZK;
typedef libsnark::default_r1cs_ppzksnark_pp ppT;
using FieldT = libff::Fr<ppT>;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

static void finalize_circuit(R1CSManager<FieldT> &mgr,
                             const map<size_t, FieldT> &assignment,
                             nkr::groth16::Circuit &out,
                             vector<FieldT> &z)
{
    mgr.gen_spmat(out.a, out.b, out.c, true); // 填充到最近的 2 的幂次，使得 A/B/C 形如 (1+2^r) * 2^s
    out.public_inputs = mgr.num_public();
    z.assign(out.a.num_cols, FieldT::zero());
    z[0] = FieldT::one();
    for (const auto &[id, val] : assignment) z[mgr.dense_index(id)] = val;
}

static void build_fibonacci(R1CSManager<FieldT> &mgr, map<size_t, FieldT> &assignment,
                            const size_t steps, const nkr::DataMode mode)
{
    if (steps < 2) throw invalid_argument("fibonacci steps must be at least 2");
    const nkr::InputSampler sampler(2, mode);
    const FieldT first_value = sampler.sample<FieldT>(0, [] { return FieldT::random_element(); });
    const FieldT second_value = sampler.sample<FieldT>(1, [] { return FieldT::random_element(); });
    FieldT previous_value = first_value;
    FieldT current_value = second_value;
    Variable<FieldT> previous(VariableType::Public, mgr);
    Variable<FieldT> current(VariableType::Public, mgr);
    const Variable<FieldT> expected(VariableType::Public, mgr);
    assignment[previous.get_id()] = first_value;
    assignment[current.get_id()] = second_value;

    for (size_t step = 2; step <= steps; ++step) {
        const Variable<FieldT> next(VariableType::Private, mgr);
        LinearCombination<FieldT> sum(previous);
        sum += current;
        mgr.add_constraint(LinearCombination<FieldT>(next), sum);
        const FieldT next_value = previous_value + current_value;
        assignment[next.get_id()] = next_value;
        previous_value = current_value;
        current_value = next_value;
        previous = current;
        current = next;
    }
    assignment[expected.get_id()] = current_value;
    mgr.add_constraint(LinearCombination<FieldT>(expected), LinearCombination<FieldT>(current));
}

static void build_matmul(R1CSManager<FieldT> &mgr, map<size_t, FieldT> &assignment,
                         size_t s, const nkr::DataMode mode)
{
    vector<Variable<FieldT>> A, B, C, P;
    for (size_t i = 0; i < s * s; i++) A.push_back(Variable<FieldT>(VariableType::Public, mgr));
    for (size_t i = 0; i < s * s; i++) B.push_back(Variable<FieldT>(VariableType::Public, mgr));
    for (size_t i = 0; i < s * s; i++) C.push_back(Variable<FieldT>(VariableType::Public, mgr));
    for (size_t i = 0; i < s * s * s; i++) P.push_back(Variable<FieldT>(VariableType::Private, mgr));

    for (size_t i = 0; i < s; i++)
        for (size_t j = 0; j < s; j++)
            for (size_t l = 0; l < s; l++)
                mgr.add_constraint(P[i * s * s + j * s + l], A[i * s + l], B[l * s + j]);

    for (size_t i = 0; i < s; i++)
        for (size_t j = 0; j < s; j++) {
            LinearCombination<FieldT> sum;
            for (size_t l = 0; l < s; l++) sum += P[i * s * s + j * s + l];
            mgr.add_constraint(C[i * s + j], sum);
        }

    vector<FieldT> av(s * s), bv(s * s);
    const nkr::InputSampler sampler(2 * s * s, mode);
    #pragma omp parallel for
    for (size_t i = 0; i < s * s; i++) {
        av[i] = sampler.sample<FieldT>(2 * i, [] { return FieldT::random_element(); });
        bv[i] = sampler.sample<FieldT>(2 * i + 1, [] { return FieldT::random_element(); });
    }

    for (size_t i = 0; i < s; i++)
        for (size_t j = 0; j < s; j++) {
            FieldT sum = FieldT::zero();
            for (size_t l = 0; l < s; l++) {
                const FieldT p = av[i * s + l] * bv[l * s + j];
                assignment[P[i * s * s + j * s + l].get_id()] = p;
                sum += p;
            }
            assignment[C[i * s + j].get_id()] = sum;
        }
    for (size_t i = 0; i < s * s; i++) {
        assignment[A[i].get_id()] = av[i];
        assignment[B[i].get_id()] = bv[i];
    }
}

static size_t parse_positive_size(const string &text)
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

static void build_poseidon(R1CSManager<FieldT> &mgr,
                           map<size_t, FieldT> &assignment,
                           const size_t count, const nkr::DataMode mode)
{
    const nkr::InputSampler sampler(2 * count, mode);
    for (size_t i = 0; i < count; ++i) {
        const FieldT left_value = sampler.sample<FieldT>(
            2 * i, [] { return FieldT::random_element(); });
        const FieldT right_value = sampler.sample<FieldT>(
            2 * i + 1, [] { return FieldT::random_element(); });
        const FieldT digest_value = Poseidon::hash2(left_value, right_value);

        // Only the digest is public: the circuit proves knowledge of a pair of
        // private field elements whose Poseidon hash equals this value.
        Variable<FieldT> digest(VariableType::Public, mgr);
        Variable<FieldT> left(VariableType::Private, mgr);
        Variable<FieldT> right(VariableType::Private, mgr);
        assignment[digest.get_id()] = digest_value;
        assignment[left.get_id()] = left_value;
        assignment[right.get_id()] = right_value;
        Poseidon::add_hash2_constraints(
            mgr, assignment, left, left_value, right, right_value, digest);
    }
}

static void build_sql_aggregate(R1CSManager<FieldT> &mgr,
                                map<size_t, FieldT> &assignment,
                                const size_t row_count, const nkr::DataMode mode)
{
    if (row_count < 2 || !SqlAggregate::is_power_of_two(row_count))
        throw invalid_argument("SQL row count must be a power of two and at least 2");

    vector<SqlAggregate::Row<FieldT>> rows;
    rows.reserve(row_count);
    const nkr::InputSampler sampler(2 * row_count, mode);
    for (size_t i = 0; i < row_count; ++i) {
        const FieldT category = sampler.sample<FieldT>(2 * i, [] {
            return FieldT(static_cast<long>(FieldT::random_element().as_ulong() % 16));
        });
        const FieldT amount = sampler.sample<FieldT>(2 * i + 1, [] {
            return FieldT(static_cast<long>(
                static_cast<uint32_t>(FieldT::random_element().as_ulong())));
        });
        rows.push_back({category, amount});
    }
    const FieldT target_value = rows.front().category;
    const auto result = SqlAggregate::evaluate(rows, target_value);

    Variable<FieldT> table_root(VariableType::Public, mgr);
    Variable<FieldT> target_category(VariableType::Public, mgr);
    Variable<FieldT> expected_count(VariableType::Public, mgr);
    Variable<FieldT> expected_sum(VariableType::Public, mgr);
    assignment[table_root.get_id()] = result.table_root;
    assignment[target_category.get_id()] = target_value;
    assignment[expected_count.get_id()] = result.count;
    assignment[expected_sum.get_id()] = result.sum;
    SqlAggregate::add_query_constraints(
        mgr, assignment, rows, table_root, target_category, target_value,
        expected_count, expected_sum);
}

static void usage(const char *prog) {
    cerr << "Usage: " << prog
         << " [--profile] [--data-mode dense|sparse|hybrid] <case> [params...]\n"
         << "  cases:\n"
         << "    matmul <size>      matrix-multiplication R1CS of dimension <size>\n"
         << "    poseidon [count]   <count> independent private-input Poseidon hashes\n"
         << "    sql-aggregate <rows>  verify COUNT/SUM over a committed private table\n"
         << "    fibonacci <steps>  compute F_steps from F0 and F1; steps >= 2\n"
         << "  data modes (applied to input values, not intermediate witness values):\n"
         << "    dense (default)    all inputs are random nonzero values\n"
         << "    sparse             half of the inputs are zero, at shuffled positions\n"
         << "    hybrid             first half dense, second half sparse\n"
         << "                       zero counts round down for odd-sized regions\n"
         << "  Outputs (in " APP_DATA_DIR "):\n"
         << "    sparse/hybrid append _<mode> to the default case/size stem\n"
         << "    <stem>.bin         R1CS (k, num_cols, A, B, C)\n"
         << "    <stem>.witness.bin satisfying witness z (size num_cols)\n";
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
    ppT::init_public_params();
    nkr::ProfileScope total("groth16.gen.total");
    const string case_name = arguments[0];

    R1CSManager<FieldT> mgr;
    map<size_t, FieldT> assignment;
    string stem;

    if (case_name == "matmul") {
        if (arguments.size() != 2) usage(argv[0]);
        const size_t s = parse_positive_size(arguments[1]);
        const size_t max_size = numeric_limits<size_t>::max();
        if (s > max_size / s || s * s > max_size / s || s * s > max_size / 2)
            throw invalid_argument("matrix size is too large");
        {
            nkr::ProfileScope stage("groth16.gen.build_matmul");
            build_matmul(mgr, assignment, s, mode);
        }
        stem = "matmul_" + to_string(s);
    } else if (case_name == "fibonacci") {
        if (arguments.size() != 2) usage(argv[0]);
        const size_t steps = parse_positive_size(arguments[1]);
        if (steps > numeric_limits<size_t>::max() / 2)
            throw invalid_argument("Fibonacci step count is too large");
        {
            nkr::ProfileScope stage("groth16.gen.build_fibonacci");
            build_fibonacci(mgr, assignment, steps, mode);
        }
        stem = "fibonacci_" + to_string(steps);
    } else if (case_name == "poseidon") {
        if (arguments.size() > 2) usage(argv[0]);
        const size_t count = arguments.size() < 2 ? 1 : parse_positive_size(arguments[1]);
        if (count > numeric_limits<size_t>::max() / 2)
            throw invalid_argument("Poseidon count is too large");
        {
            nkr::ProfileScope stage("groth16.gen.build_poseidon");
            build_poseidon(mgr, assignment, count, mode);
        }
        stem = "poseidon_" + to_string(count);
    } else if (case_name == "sql-aggregate") {
        if (arguments.size() != 2) usage(argv[0]);
        const size_t rows = parse_positive_size(arguments[1]);
        if (rows > numeric_limits<size_t>::max() / 2)
            throw invalid_argument("SQL row count is too large");
        {
            nkr::ProfileScope stage("groth16.gen.build_sql_aggregate");
            build_sql_aggregate(mgr, assignment, rows, mode);
        }
        stem = "sql_aggregate_" + to_string(rows);
    } else {
        cerr << "Unknown case: " << case_name << "\n";
        usage(argv[0]);
    }
    if (mode != nkr::DataMode::Dense)
        stem += "_" + string(nkr::data_mode_name(mode));
    cout << "Data mode: " << nkr::data_mode_name(mode) << '\n';

    nkr::groth16::Circuit out;
    vector<FieldT> z;
    {
        nkr::ProfileScope stage("groth16.gen.finalize_circuit");
        finalize_circuit(mgr, assignment, out, z);
    }

    printf("* R1CS constraints: %zu\n", out.a.num_rows);
    printf("* R1CS variables:   %zu (public: %zu, private: %zu)\n",
           out.a.num_cols, out.public_inputs, out.a.num_cols - 1 - out.public_inputs);

    const string data_dir = APP_DATA_DIR;
    std::filesystem::create_directories(data_dir);
    const auto paths = nkr::groth16::artifact_paths(std::filesystem::path(data_dir), stem);

    {
        nkr::ProfileScope stage("groth16.gen.write_circuit");
        nkr::groth16::write_circuit(paths.r1cs, out);
    }
    cout << "Wrote R1CS to " << paths.r1cs << endl;

    {
        nkr::ProfileScope stage("groth16.gen.write_witness");
        nkr::groth16::write_witness(paths.witness, z);
    }
    cout << "Wrote witness to " << paths.witness << endl;

    return 0;
} catch (const exception &error) {
    cerr << "gen: " << error.what() << '\n';
    return 1;
}
