#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

/*
    Sparse-matrix storage hierarchy.

    SparseMatrix is the direction-agnostic base: it only carries the matrix
    shape and the non-zero values.  The per-direction index arrays live in the
    two dual derived types:

        CsrMatrix  -- row-major  (row_ptr + col_idx), the canonical R1CS layout
        CscMatrix  -- col-major  (col_ptr + row_idx), the transpose-friendly view

    The two derived types are implemented as exact duals of each other: both
    store their indices as uint32_t and each exposes a symmetric conversion
    pair (from_csr/to_csr, from_csc/to_csc) built on the same counting-sort
    routine with the roles of row/col swapped.  Converting never swaps the
    shape: CsrMatrix and CscMatrix are two representations of the *same*
    matrix, so num_rows/num_cols are carried over unchanged.
*/

template <typename S>
struct SparseMatrix {
    size_t num_rows = 0, num_cols = 0;
    std::vector<S> values;

    size_t nnz() const { return values.size(); }
};

template <typename S> struct CscMatrix;

// Row-major (CSR): rows indexed by row_ptr, non-zeros stored row by row.
template <typename S>
struct CsrMatrix : SparseMatrix<S> {
    std::vector<uint32_t> row_ptr, col_idx;

    void randomize(size_t rows, size_t cols);   // two random non-zeros per row

    CscMatrix<S> to_csc() const;                // dual view of the same matrix
    static CsrMatrix<S> from_csc(const CscMatrix<S> &csc);
};

// Column-major (CSC): columns indexed by col_ptr, non-zeros stored col by col.
template <typename S>
struct CscMatrix : SparseMatrix<S> {
    std::vector<uint32_t> col_ptr, row_idx;

    CsrMatrix<S> to_csr() const;                // dual view of the same matrix
    static CscMatrix<S> from_csr(const CsrMatrix<S> &csr);
};

// ---------------------------------------------------------------------------
//  Method bodies (defined after both structs are complete so the two
//  from_*/to_* conversions can refer to each other).
// ---------------------------------------------------------------------------

template <typename S>
void CsrMatrix<S>::randomize(size_t rows, size_t cols) {
    this->num_rows = rows;
    this->num_cols = cols;
    row_ptr.resize(rows + 1);
    col_idx.resize(rows * 2);
    this->values.resize(rows * 2);

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, cols - 1);

    #pragma omp parallel for
    for (size_t r = 0; r < rows; r++) {
        size_t c1 = dist(rng);
        size_t c2 = dist(rng);
        while (c2 == c1) c2 = dist(rng);

        col_idx[r * 2] = static_cast<uint32_t>(c1);
        col_idx[r * 2 + 1] = static_cast<uint32_t>(c2);

        this->values[r * 2] = S::random_element();
        this->values[r * 2 + 1] = S::random_element();
    }

    row_ptr[0] = 0;
    for (size_t r = 0; r < rows; r++) row_ptr[r + 1] = static_cast<uint32_t>(row_ptr[r] + 2);
}

// CSR -> CSC: for each column, list its rows.  Shape is carried over unchanged.
template <typename S>
CscMatrix<S> CscMatrix<S>::from_csr(const CsrMatrix<S> &csr) {
    CscMatrix<S> csc;
    csc.num_rows = csr.num_rows;
    csc.num_cols = csr.num_cols;

    const size_t nnz = csr.col_idx.size();
    csc.col_ptr.assign(csr.num_cols + 1, 0);
    for (size_t j = 0; j < nnz; j++) csc.col_ptr[csr.col_idx[j] + 1]++;
    for (size_t c = 0; c < csr.num_cols; c++) csc.col_ptr[c + 1] += csc.col_ptr[c];

    csc.row_idx.resize(nnz);
    csc.values.resize(nnz);

    std::vector<uint32_t> cursor = csc.col_ptr;
    for (size_t r = 0; r < csr.num_rows; r++)
        for (size_t j = csr.row_ptr[r]; j < csr.row_ptr[r + 1]; j++) {
            const size_t c = csr.col_idx[j];
            const size_t pos = cursor[c]++;
            csc.row_idx[pos] = static_cast<uint32_t>(r);
            csc.values[pos] = csr.values[j];
        }
    return csc;
}

// CSC -> CSR: the exact dual of from_csr, with the roles of row/col swapped.
template <typename S>
CsrMatrix<S> CsrMatrix<S>::from_csc(const CscMatrix<S> &csc) {
    CsrMatrix<S> csr;
    csr.num_rows = csc.num_rows;
    csr.num_cols = csc.num_cols;

    const size_t nnz = csc.row_idx.size();
    csr.row_ptr.assign(csc.num_rows + 1, 0);
    for (size_t j = 0; j < nnz; j++) csr.row_ptr[csc.row_idx[j] + 1]++;
    for (size_t r = 0; r < csc.num_rows; r++) csr.row_ptr[r + 1] += csr.row_ptr[r];

    csr.col_idx.resize(nnz);
    csr.values.resize(nnz);

    std::vector<uint32_t> cursor = csr.row_ptr;
    for (size_t c = 0; c < csc.num_cols; c++)
        for (size_t j = csc.col_ptr[c]; j < csc.col_ptr[c + 1]; j++) {
            const size_t r = csc.row_idx[j];
            const size_t pos = cursor[r]++;
            csr.col_idx[pos] = static_cast<uint32_t>(c);
            csr.values[pos] = csc.values[j];
        }
    return csr;
}

template <typename S>
CscMatrix<S> CsrMatrix<S>::to_csc() const { return CscMatrix<S>::from_csr(*this); }

template <typename S>
CsrMatrix<S> CscMatrix<S>::to_csr() const { return CsrMatrix<S>::from_csc(*this); }
