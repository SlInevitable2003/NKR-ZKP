#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "libff/common/profiling.hpp"
#include <libff/common/utils.hpp>
#include "libff/algebra/scalar_multiplication/multiexp.hpp"

#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"

#include "utils/binary_archive.cuh"   /* BinaryArchive (host-side I/O only) */

using namespace std;
typedef libsnark::default_r1cs_ppzksnark_pp ppT;

#ifndef APP_DATA_DIR
#define APP_DATA_DIR "app/data"
#endif

template <typename ppT>
struct Groth16Proof {
    libff::G1<ppT> g_A;
    libff::G2<ppT> g_B;
    libff::G1<ppT> g_C;
};

template <typename ppT>
struct Groth16VerificationKeys {
    libff::GT<ppT> alpha_g1_beta_g2;
    libff::G2<ppT> gamma_g2;
    libff::G2<ppT> delta_g2;
    vector<libff::G1<ppT>> IC;
};

template <typename ppT>
static bool extract_public_input(const vector<libff::Fr<ppT>> &z, size_t k, vector<libff::Fr<ppT>> &pub)
{
    if (z.size() < 1 + k) return false;
    pub.assign(z.begin() + 1, z.begin() + 1 + k);
    return true;
}

template <typename ppT>
static bool verify_groth16(const Groth16VerificationKeys<ppT> &vk, const vector<libff::Fr<ppT>> &public_input, const Groth16Proof<ppT> &proof)
{
    if (vk.IC.size() < 1 + public_input.size()) {
        cerr << "IC size (" << vk.IC.size() << ") < 1 + #public inputs ("
             << (1 + public_input.size()) << ").\n";
        return false;
    }

    libff::enter_block("Accumulate public input");
    libff::G1<ppT> acc = vk.IC[0];
    if (!public_input.empty()) {
        acc = acc + libff::multi_exp<libff::G1<ppT>, libff::Fr<ppT>, libff::multi_exp_method::multi_exp_method_BDLO12>
            (vk.IC.begin() + 1, vk.IC.end(), public_input.begin(), public_input.end(), 1);
    }
    libff::leave_block("Accumulate public input");

    libff::enter_block("Pairing checks");
    const libff::GT<ppT> lhs = ppT::reduced_pairing(proof.g_A, proof.g_B);
    const libff::GT<ppT> rhs = vk.alpha_g1_beta_g2 * ppT::reduced_pairing(acc, vk.gamma_g2) * ppT::reduced_pairing(proof.g_C, vk.delta_g2);
    libff::leave_block("Pairing checks");

    return lhs == rhs;
}

static void usage(const char *prog) {
    cerr << "Usage: " << prog << " [--path <dir>] <r1cs-file>\n"
         << "  Verifies <stem>.proof.bin against the verification key <stem>.vk.bin\n"
         << "  (from setup.cu) and the public inputs in <stem>.witness.bin (from gen.cu).\n";
    exit(1);
}

int main(int argc, char *argv[]) {
    ppT::init_public_params();

    string data_dir = APP_DATA_DIR;
    string filename;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--path") {
            if (i + 1 >= argc) usage(argv[0]);
            data_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
        } else if (!arg.empty() && arg[0] == '-') {
            cerr << "Unknown option: " << arg << "\n";
            usage(argv[0]);
        } else {
            filename = arg;
        }
    }
    if (filename.empty()) usage(argv[0]);

    string stem = filename;
    const string ext = ".bin";
    if (stem.size() >= ext.size() && stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0) stem.erase(stem.size() - ext.size());
    const string r1cs_path    = data_dir + "/" + filename;
    const string vk_path      = data_dir + "/" + stem + ".vk.bin";
    const string proof_path   = data_dir + "/" + stem + ".proof.bin";
    const string witness_path = data_dir + "/" + stem + ".witness.bin";

    libff::enter_block("Reading R1CS header");
    BinaryArchive ar;
    ar.open_for_read(r1cs_path);
    
    size_t k, num_cols;
    ar.read(k); ar.read(num_cols); ar.close();
    libff::leave_block("Reading R1CS header");

    cout << "Loaded R1CS header from " << r1cs_path << endl;
    cout << "* public inputs : " << k << "\n"
         << "* variables     : " << (num_cols - 1) << "\n";

    libff::enter_block("Reading verification key from file");
    BinaryArchive var;
    var.open_for_read(vk_path);
    
    Groth16VerificationKeys<ppT> vk;
    var.read(vk.alpha_g1_beta_g2); var.read(vk.gamma_g2); var.read(vk.delta_g2); var.read(vk.IC); var.close();
    libff::leave_block("Reading verification key from file");

    cout << "Loaded verification key from " << vk_path << endl;
    if (vk.IC.size() != k + 1) {
        cerr << "Verification key / R1CS mismatch (vk_IC size " << vk.IC.size()
             << " != k+1 = " << (k + 1) << ").\n";
        return 1;
    }

    libff::enter_block("Reading witness from file");
    BinaryArchive war;
    war.open_for_read(witness_path);
    vector<libff::Fr<ppT>> z;
    war.read(z); war.close();
    libff::leave_block("Reading witness from file");

    if (z.size() != num_cols) {
        cerr << "Witness size mismatch (expected num_cols = " << num_cols
             << ", got " << z.size() << ").\n";
        return 1;
    }
    vector<libff::Fr<ppT>> public_input;
    if (!extract_public_input<ppT>(z, k, public_input)) {
        cerr << "Witness too short for " << k << " public inputs.\n";
        return 1;
    }

    libff::enter_block("Reading proof from file");
    BinaryArchive pr;
    pr.open_for_read(proof_path);
    Groth16Proof<ppT> proof;
    pr.read(proof.g_A); pr.read(proof.g_B); pr.read(proof.g_C); pr.close();
    libff::leave_block("Reading proof from file");
    cout << "Loaded proof from " << proof_path << endl;

    cout << "* public inputs : " << k << "\n";
    libff::enter_block("Groth16 Verify");
    const bool ok = verify_groth16<ppT>(vk, public_input, proof);
    libff::leave_block("Groth16 Verify");

    cout << (ok ? "Proof verification succeeded." : "Proof verification FAILED.") << endl;
    return ok ? 0 : 1;
}
