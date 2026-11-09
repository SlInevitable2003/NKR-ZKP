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
#include "profile.hpp"
#include "r1cs/poseidon.hpp"
#include "r1cs/r1cs.hpp"

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

static void build_matmul(R1CSManager<FieldT> &mgr, map<size_t, FieldT> &assignment, size_t s)
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
    #pragma omp parallel for
    for (size_t i = 0; i < s * s; i++) {
        av[i] = FieldT::random_element();
        bv[i] = FieldT::random_element();
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
                           const size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const FieldT left_value = FieldT::random_element();
        const FieldT right_value = FieldT::random_element();
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

static void usage(const char *prog) {
    cerr << "Usage: " << prog << " [--profile] <case> [params...]\n"
         << "  cases:\n"
         << "    matmul <size>      matrix-multiplication R1CS of dimension <size>\n"
         << "    poseidon [count]   <count> independent private-input Poseidon hashes\n"
         << "  Outputs (in " APP_DATA_DIR "):\n"
         << "    <stem>.bin         R1CS (k, num_cols, A, B, C)\n"
         << "    <stem>.witness.bin satisfying witness z (size num_cols)\n";
    exit(1);
}

int main(int argc, char *argv[]) {
    bool profile = false;
    vector<string> arguments;
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "--profile") profile = true;
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
        if (arguments.size() < 2) usage(argv[0]);
        const size_t s = (size_t)atoi(arguments[1].c_str());
        if (s == 0) usage(argv[0]);
        {
            nkr::ProfileScope stage("groth16.gen.build_matmul");
            build_matmul(mgr, assignment, s);
        }
        stem = "matmul_" + to_string(s);
    } else if (case_name == "poseidon") {
        const size_t count = arguments.size() < 2 ? 1 : parse_positive_size(arguments[1]);
        {
            nkr::ProfileScope stage("groth16.gen.build_poseidon");
            build_poseidon(mgr, assignment, count);
        }
        stem = "poseidon_" + to_string(count);
    } else {
        cerr << "Unknown case: " << case_name << "\n";
        usage(argv[0]);
    }

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
}
