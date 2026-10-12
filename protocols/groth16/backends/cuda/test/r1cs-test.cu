#include <iostream>
#include <iomanip>
#include <cassert>
#include "libff/common/profiling.hpp"
#include "libsnark/common/default_types/r1cs_ppzksnark_pp.hpp"

#include "api.h"
typedef libsnark::default_r1cs_ppzksnark_pp ppT;

int main() {
    using namespace LightZK;
    ppT::init_public_params();

    const int n = 2;               // 2×2 矩阵乘法

    R1CSManager<libff::Fr<ppT>> mgr;

    // ---- 创建变量 ----
    std::vector<Variable<libff::Fr<ppT>>> A, B, C, P;
    for (int i = 0; i < n * n; ++i)     A.push_back(Variable<libff::Fr<ppT>>(VariableType::Public,  mgr));
    for (int i = 0; i < n * n; ++i)     B.push_back(Variable<libff::Fr<ppT>>(VariableType::Public,  mgr));
    for (int i = 0; i < n * n; ++i)     C.push_back(Variable<libff::Fr<ppT>>(VariableType::Public,  mgr));
    for (int i = 0; i < n * n * n; ++i) P.push_back(Variable<libff::Fr<ppT>>(VariableType::Private, mgr));

    // ---- 乘法约束：P[i,j,k] = A[i,k] * B[k,j] ----
    for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
    for (int k = 0; k < n; ++k)
        mgr.add_constraint(P[i * n * n + j * n + k],
                           A[i * n + k],
                           B[k * n + j]);

    // ---- 线性约束：C[i,j] = Σ_k P[i,j,k] ----
    for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
        LinearCombination<libff::Fr<ppT>> sum;
        for (int k = 0; k < n; ++k)
            sum += P[i * n * n + j * n + k];
        mgr.add_constraint(C[i * n + j], sum);
    }

    // ---- 生成 R1CS 矩阵 ----
    CsrMatrix<libff::Fr<ppT>> A_mat, B_mat, C_mat;
    mgr.gen_spmat(A_mat, B_mat, C_mat, true);

    size_t n_rows = A_mat.num_rows;
    size_t n_cols = A_mat.num_cols;

    // ---- 按稠密格式打印 ----
    auto print_dense = [&](const char* name, const CsrMatrix<libff::Fr<ppT>>& mat) {
        std::cout << name << " (" << n_rows << " x " << n_cols << "):\n";
        for (size_t r = 0; r < n_rows; ++r) {
            std::vector<libff::Fr<ppT>> row(n_cols, libff::Fr<ppT>{0});
            for (size_t p = mat.row_ptr[r]; p < mat.row_ptr[r + 1]; ++p)
                row[mat.col_idx[p]] = mat.values[p];
            for (size_t c = 0; c < n_cols; ++c)
                std::cout << std::setw(3) << row[c] << " ";
            std::cout << "\n";
        }
        std::cout << "\n";
    };

    std::cout << "Constraints: " << n_rows << ", Variables: " << n_cols << "\n\n";

    print_dense("A", A_mat);
    print_dense("B", B_mat);
    print_dense("C", C_mat);

    // ---- CSR -> CSC -> CSR 对偶往返：结构和形状必须逐位一致 ----
    for (const CsrMatrix<libff::Fr<ppT>> *m : { &A_mat, &B_mat, &C_mat }) {
        const CsrMatrix<libff::Fr<ppT>> roundtrip = m->to_csc().to_csr();
        assert(roundtrip.num_rows == m->num_rows && roundtrip.num_cols == m->num_cols);
        assert(roundtrip.row_ptr == m->row_ptr);
        assert(roundtrip.col_idx == m->col_idx);
        assert(roundtrip.values == m->values);
    }
    std::cout << "CSR<->CSC round-trip: OK\n";

    return 0;
}
