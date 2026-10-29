#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "libfqfft/evaluation_domain/get_evaluation_domain.hpp"
#include "nkr/operators/cuda.hpp"
#include "plonk/protocol.hpp"
#include "plonk/types.hpp"

using nkr::plonk::Curve;
using nkr::plonk::Scalar;
using namespace alt_bn128;

namespace {

void require_equal(const std::vector<Scalar> &expected,
                   const std::vector<Scalar> &actual,
                   const char *operation)
{
    if (expected.size() != actual.size())
        throw std::runtime_error(std::string(operation) + ": size mismatch");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i])
            throw std::runtime_error(std::string(operation) +
                                     ": mismatch at index " + std::to_string(i));
    }
}

Scalar evaluate(const std::vector<Scalar> &coefficients, const Scalar &point)
{
    Scalar result = Scalar::zero();
    for (auto i = coefficients.rbegin(); i != coefficients.rend(); ++i)
        result = result * point + *i;
    return result;
}

std::vector<Scalar> synthetic_division(const std::vector<Scalar> &polynomial,
                                       const Scalar &point)
{
    std::vector<Scalar> quotient(polynomial.size() - 1);
    quotient.back() = polynomial.back();
    for (std::size_t i = quotient.size() - 1; i > 0; --i)
        quotient[i - 1] = polynomial[i] + point * quotient[i];
    return quotient;
}

} // namespace

int main()
{
    Curve::init_public_params();
    constexpr std::size_t n = 4096;
    const Scalar omega =
        reinterpret_cast<const Scalar *>(forward_roots_of_unity)[libff::log2(n)];
    const Scalar coset = Scalar::multiplicative_generator;

    std::vector<Scalar> values(n);
    for (Scalar &value : values) value = Scalar::random_element();
    fr_t *device_values = nullptr;
    CUDA_CHECK(cudaMalloc(&device_values, n * sizeof(fr_t)));

    NTTContext<fr_t, Scalar> ntt(n, omega, coset);
    CUDA_CHECK(cudaMemcpy(device_values, values.data(), n * sizeof(fr_t),
                          cudaMemcpyHostToDevice));
    auto expected_ntt = values;
    libfqfft::get_evaluation_domain<Scalar>(n)->FFT(expected_ntt);
    ntt.ntt(device_values);
    std::vector<Scalar> actual(n);
    CUDA_CHECK(cudaMemcpy(actual.data(), device_values, n * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    require_equal(expected_ntt, actual, "NTT");

    ntt.intt(device_values);
    CUDA_CHECK(cudaMemcpy(actual.data(), device_values, n * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    require_equal(values, actual, "inverse NTT");

    CUDA_CHECK(cudaMemcpy(device_values, values.data(), n * sizeof(fr_t),
                          cudaMemcpyHostToDevice));
    auto expected_coset = values;
    libfqfft::get_evaluation_domain<Scalar>(n)->cosetFFT(expected_coset, coset);
    ntt.coset_ntt(device_values);
    CUDA_CHECK(cudaMemcpy(actual.data(), device_values, n * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    require_equal(expected_coset, actual, "coset NTT");
    ntt.coset_intt(device_values);
    CUDA_CHECK(cudaMemcpy(actual.data(), device_values, n * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    require_equal(values, actual, "inverse coset NTT");

    std::vector<Scalar> nonzero(n);
    for (Scalar &value : nonzero) {
        do value = Scalar::random_element(); while (value.is_zero());
    }
    CUDA_CHECK(cudaMemcpy(device_values, nonzero.data(), n * sizeof(fr_t),
                          cudaMemcpyHostToDevice));
    pointwise_inverse(device_values, n);
    CUDA_CHECK(cudaMemcpy(actual.data(), device_values, n * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    for (std::size_t i = 0; i < n; ++i)
        if (actual[i] != nonzero[i].inverse())
            throw std::runtime_error("pointwise inverse mismatch at index " +
                                     std::to_string(i));

    CUDA_CHECK(cudaMemcpy(device_values, nonzero.data(), n * sizeof(fr_t),
                          cudaMemcpyHostToDevice));
    mgpu::CuScanContext<fr_t> scan(n);
    scan.inclusive(device_values, device_values, n, FieldMultiply<fr_t>{},
                   device_value<fr_t>(Scalar::one()));
    std::vector<Scalar> expected_scan(n);
    expected_scan[0] = nonzero[0];
    for (std::size_t i = 1; i < n; ++i)
        expected_scan[i] = expected_scan[i - 1] * nonzero[i];
    CUDA_CHECK(cudaMemcpy(actual.data(), device_values, n * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    require_equal(expected_scan, actual, "inclusive product scan");

    std::vector<Scalar> polynomial(n + 3);
    for (Scalar &coefficient : polynomial)
        coefficient = Scalar::random_element();
    const Scalar point = Scalar::random_element();
    fr_t *device_polynomial = nullptr;
    fr_t *device_quotient = nullptr;
    CUDA_CHECK(cudaMalloc(&device_polynomial, polynomial.size() * sizeof(fr_t)));
    CUDA_CHECK(cudaMalloc(&device_quotient,
                          (polynomial.size() - 1) * sizeof(fr_t)));
    CUDA_CHECK(cudaMemcpy(device_polynomial, polynomial.data(),
                          polynomial.size() * sizeof(fr_t), cudaMemcpyHostToDevice));

    PolynomialEvaluationContext<fr_t, Scalar> evaluation(polynomial.size());
    if (evaluation.evaluate(device_polynomial, polynomial.size(), point) !=
        evaluate(polynomial, point))
        throw std::runtime_error("polynomial evaluation mismatch");

    SyntheticDivisionContext<fr_t, Scalar> division(polynomial.size());
    division.divide(device_polynomial, polynomial.size(), point, device_quotient);
    const auto expected_quotient = synthetic_division(polynomial, point);
    std::vector<Scalar> actual_quotient(expected_quotient.size());
    CUDA_CHECK(cudaMemcpy(actual_quotient.data(), device_quotient,
                          actual_quotient.size() * sizeof(fr_t),
                          cudaMemcpyDeviceToHost));
    require_equal(expected_quotient, actual_quotient, "synthetic division");

    const auto srs = nkr::plonk::generate_srs(polynomial.size());
    BucketContext<fr_t, Scalar> buckets(n, 8);
    MSMContext<fr_t, g1_t::affine_t, g1_t, g1_bucket_t, Scalar,
               nkr::plonk::G1> msm(n, 8);
    std::vector<nkr::plonk::G1> affine_bases(
        srs.g1_powers.begin(), srs.g1_powers.begin() + n);
    nkr::plonk::G1::batch_to_special_all_non_zeros(affine_bases);
    msm.load_bases(affine_bases.data());
    buckets.load_scalars(device_polynomial);
    buckets.process();
    nkr::plonk::G1 actual_commitment;
    msm.msm(buckets, &actual_commitment);
    for (std::size_t i = n; i < polynomial.size(); ++i)
        actual_commitment = actual_commitment + polynomial[i] * srs.g1_powers[i];
    nkr::plonk::G1 expected_commitment = nkr::plonk::G1::zero();
    for (std::size_t i = 0; i < polynomial.size(); ++i)
        expected_commitment =
            expected_commitment + polynomial[i] * srs.g1_powers[i];
    if (actual_commitment != expected_commitment)
        throw std::runtime_error("MSM commitment mismatch");

    CUDA_CHECK(cudaFree(device_quotient));
    CUDA_CHECK(cudaFree(device_polynomial));
    CUDA_CHECK(cudaFree(device_values));
    std::cout << "PLONK CUDA operator tests passed.\n";
    return EXIT_SUCCESS;
}
