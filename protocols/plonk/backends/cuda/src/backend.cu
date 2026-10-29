#include "plonk/backend.hpp"

#include <array>
#include <memory>
#include <stdexcept>

#include "libff/common/utils.hpp"
#include "nkr/operators/cuda.hpp"
#include "plonk/transcript.hpp"
#include "profile.hpp"

namespace nkr::plonk {
namespace {

using namespace alt_bn128;

enum FixedPolynomial : std::size_t {
    QL, QR, QM, QO, QC, SIGMA1, SIGMA2, SIGMA3, FIXED_POLYNOMIALS
};

unsigned choose_window_bits(const std::size_t terms)
{
    if (terms <= 4096) return 8;
    if (terms <= 65536) return 11;
    return 13;
}

void profile_synchronize()
{
    if (!profiling_enabled()) return;
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaGetLastError());
}

VerificationKey verification_key_from(const ProvingKey &key)
{
    return {key.domain, key.commitments, key.srs.g2, key.srs.tau_g2};
}

std::array<const PolynomialData *, FIXED_POLYNOMIALS>
fixed_polynomials(const ProvingKey &key)
{
    return {&key.fixed.q_l, &key.fixed.q_r, &key.fixed.q_m,
            &key.fixed.q_o, &key.fixed.q_c, &key.fixed.sigma_1,
            &key.fixed.sigma_2, &key.fixed.sigma_3};
}

class CudaProveLayout {
public:
    const std::size_t n;
    const std::size_t quotient_size;

    BucketContext<fr_t, Scalar> commitment_buckets;
    NTTContext<fr_t, Scalar> domain_ntt;
    NTTContext<fr_t, Scalar> quotient_ntt;
    MSMContext<fr_t, g1_t::affine_t, g1_t, g1_bucket_t, Scalar, G1>
        commitment_msm;
    mgpu::CuScanContext<fr_t> product_scan;
    PolynomialEvaluationContext<fr_t, Scalar> polynomial_evaluation;
    SyntheticDivisionContext<fr_t, Scalar> synthetic_division;

    fr_t *fixed_evaluations[FIXED_POLYNOMIALS];
    fr_t *fixed_coefficients[FIXED_POLYNOMIALS];
    fr_t *fixed_coset_evaluations[FIXED_POLYNOMIALS];
    fr_t *omegas;
    fr_t *wire_evaluations[3];
    fr_t *wire_coefficients[3];
    fr_t *z_coefficients;
    fr_t *public_input_coefficients;
    fr_t *wire_coset_evaluations[3];
    fr_t *z_coset_evaluations;
    fr_t *public_input_coset_evaluations;
    fr_t *quotient_evaluations;
    fr_t *permutation_numerator;
    fr_t *permutation_denominator;
    fr_t *z_evaluations;
    fr_t *coset_points;
    fr_t *z_h_evaluations;
    fr_t *quotient_denominators;
    fr_t *t_parts[3];
    fr_t *linearization;
    fr_t *t_partial;
    fr_t *batched_polynomial;
    fr_t *opening_polynomials[2];
    fr_t *batching_powers;

    TypedGpuArena arena;

    explicit CudaProveLayout(const ProvingKey &key)
        : n(key.domain.domain_size),
          quotient_size(key.domain.quotient_domain_size),
          commitment_buckets(n, choose_window_bits(n)),
          domain_ntt(n, key.domain.omega, key.domain.coset_shift),
          quotient_ntt(
              quotient_size,
              reinterpret_cast<const Scalar *>(forward_roots_of_unity)
                  [libff::log2(quotient_size)],
              key.domain.coset_shift),
          commitment_msm(n, choose_window_bits(n)),
          product_scan(n),
          polynomial_evaluation(n + 3),
          synthetic_division(n + 3)
    {
        for (std::size_t i = 0; i < FIXED_POLYNOMIALS; ++i) {
            arena.register_alloc(fixed_evaluations[i], n);
            arena.register_alloc(fixed_coefficients[i], n);
            arena.register_alloc(fixed_coset_evaluations[i], quotient_size);
        }
        arena.register_alloc(omegas, n);
        for (std::size_t i = 0; i < 3; ++i) {
            arena.register_alloc(wire_evaluations[i], n);
            arena.register_alloc(wire_coefficients[i], n + 3);
            arena.register_alloc(wire_coset_evaluations[i], quotient_size);
            arena.register_alloc(t_parts[i], n + 3);
        }
        arena.register_alloc(z_coefficients, n + 3);
        arena.register_alloc(public_input_coefficients, n);
        arena.register_alloc(z_coset_evaluations, quotient_size);
        arena.register_alloc(public_input_coset_evaluations, quotient_size);
        arena.register_alloc(quotient_evaluations, quotient_size);
        arena.register_alloc(permutation_numerator, n);
        arena.register_alloc(permutation_denominator, n);
        arena.register_alloc(z_evaluations, n);
        arena.register_alloc(coset_points, quotient_size);
        arena.register_alloc(z_h_evaluations, quotient_size);
        arena.register_alloc(quotient_denominators, 2 * quotient_size);
        arena.register_alloc(linearization, n + 3);
        arena.register_alloc(t_partial, n + 3);
        arena.register_alloc(batched_polynomial, n + 3);
        arena.register_alloc(opening_polynomials[0], n + 2);
        arena.register_alloc(opening_polynomials[1], n + 2);
        arena.register_alloc(batching_powers, 7);
        arena.commit("PLONK CUDA prove");
    }
};

void upload_inputs(CudaProveLayout &layout,
                   const ProvingKey &key,
                   const Witness &witness)
{
    std::vector<G1> commitment_bases(
        key.srs.g1_powers.begin(), key.srs.g1_powers.begin() + layout.n);
    G1::batch_to_special_all_non_zeros(commitment_bases);
    layout.commitment_msm.load_bases(commitment_bases.data());
    const auto fixed = fixed_polynomials(key);
    for (std::size_t i = 0; i < FIXED_POLYNOMIALS; ++i) {
        CUDA_CHECK(cudaMemcpy(layout.fixed_evaluations[i],
                              fixed[i]->evaluations.data(),
                              layout.n * sizeof(fr_t), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(layout.fixed_coefficients[i],
                              fixed[i]->coefficients.data(),
                              layout.n * sizeof(fr_t), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(layout.fixed_coset_evaluations[i],
                              fixed[i]->coset_evaluations.data(),
                              layout.quotient_size * sizeof(fr_t),
                              cudaMemcpyHostToDevice));
    }
    CUDA_CHECK(cudaMemcpy(layout.omegas, key.omegas.data(),
                          layout.n * sizeof(fr_t), cudaMemcpyHostToDevice));

    const std::array<const std::vector<Scalar> *, 3> wires = {
        &witness.a, &witness.b, &witness.c};
    for (std::size_t i = 0; i < 3; ++i) {
        CUDA_CHECK(cudaMemcpy(layout.wire_evaluations[i], wires[i]->data(),
                              layout.n * sizeof(fr_t), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemset(layout.wire_coefficients[i], 0,
                              (layout.n + 3) * sizeof(fr_t)));
        CUDA_CHECK(cudaMemcpy(layout.wire_coefficients[i], wires[i]->data(),
                              layout.n * sizeof(fr_t), cudaMemcpyHostToDevice));
    }
    CUDA_CHECK(cudaMemset(layout.z_coefficients, 0,
                          (layout.n + 3) * sizeof(fr_t)));
    CUDA_CHECK(cudaMemcpy(layout.public_input_coefficients,
                          witness.public_input.data(),
                          layout.n * sizeof(fr_t), cudaMemcpyHostToDevice));
}

G1 commit_polynomial(CudaProveLayout &layout,
                     const ProvingKey &key,
                     fr_t *coefficients,
                     const std::size_t coefficient_count)
{
    layout.commitment_buckets.load_scalars(coefficients);
    layout.commitment_buckets.process();
    G1 result;
    layout.commitment_msm.msm(layout.commitment_buckets, &result);

    if (coefficient_count > layout.n) {
        const std::size_t tail_size = coefficient_count - layout.n;
        std::array<Scalar, 3> tail{};
        CUDA_CHECK(cudaMemcpy(tail.data(), coefficients + layout.n,
                              tail_size * sizeof(fr_t), cudaMemcpyDeviceToHost));
        for (std::size_t i = 0; i < tail_size; ++i)
            result = result + tail[i] * key.srs.g1_powers[layout.n + i];
    }
    return result;
}

void copy_to_coset(fr_t *destination,
                   const fr_t *source,
                   const std::size_t source_size,
                   CudaProveLayout &layout)
{
    CUDA_CHECK(cudaMemset(destination, 0,
                          layout.quotient_size * sizeof(fr_t)));
    CUDA_CHECK(cudaMemcpy(destination, source, source_size * sizeof(fr_t),
                          cudaMemcpyDeviceToDevice));
    layout.quotient_ntt.coset_ntt(destination);
}

__global__ void permutation_products_kernel(
    const fr_t *a,
    const fr_t *b,
    const fr_t *c,
    const fr_t *omegas,
    const fr_t *sigma_1,
    const fr_t *sigma_2,
    const fr_t *sigma_3,
    fr_t beta,
    fr_t gamma,
    fr_t k_1,
    fr_t k_2,
    fr_t *numerator,
    fr_t *denominator,
    const std::size_t count)
{
    const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    numerator[i] =
        (a[i] + beta * omegas[i] + gamma) *
        (b[i] + beta * k_1 * omegas[i] + gamma) *
        (c[i] + beta * k_2 * omegas[i] + gamma);
    denominator[i] =
        (a[i] + beta * sigma_1[i] + gamma) *
        (b[i] + beta * sigma_2[i] + gamma) *
        (c[i] + beta * sigma_3[i] + gamma);
}

class CudaBackend final : public Backend {
public:
    const char *name() const override { return "cuda"; }

    void setup(const Circuit &circuit,
               const UniversalSrs &srs,
               ProvingKey &proving_key,
               VerificationKey &verification_key) override
    {
        ProfileScope total("plonk.cuda.setup.total");
        make_cpu_backend()->setup(circuit, srs, proving_key, verification_key);
    }

    void prove(const ProvingKey &key,
               const Witness &witness,
               Proof &proof) override
    {
        ProfileScope total("plonk.cuda.prove.total");
        const std::size_t n = key.domain.domain_size;
        const std::size_t quotient_size = key.domain.quotient_domain_size;
        const std::size_t part_size = n + 2;
        CudaProveLayout layout(key);
        Transcript transcript = initialize_transcript(
            verification_key_from(key), witness.public_input);

        {
            ProfileScope stage("plonk.cuda.prove.upload_inputs");
            upload_inputs(layout, key, witness);
            profile_synchronize();
        }

        Scalar beta = Scalar::zero(), gamma = Scalar::zero();
        {
            ProfileScope round("plonk.cuda.prove.round1.total");
            {
                ProfileScope stage("plonk.cuda.prove.round1.wire_intt");
                for (fr_t *coefficients : layout.wire_coefficients)
                    layout.domain_ntt.intt(coefficients);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round1.wire_blinding");
                for (std::size_t wire = 0; wire < 3; ++wire) {
                    const Scalar linear_blind = Scalar::random_element();
                    const Scalar constant_blind = Scalar::random_element();
                    kernel<<<1, 1>>>([] __device__(fr_t *coefficients,
                                                   std::size_t size,
                                                   fr_t linear,
                                                   fr_t constant) {
                        coefficients[0] -= constant;
                        coefficients[1] -= linear;
                        coefficients[size] += constant;
                        coefficients[size + 1] += linear;
                    }, layout.wire_coefficients[wire], n,
                        device_value<fr_t>(linear_blind),
                        device_value<fr_t>(constant_blind));
                }
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round1.wire_msm");
                proof.a_commitment = commit_polynomial(
                    layout, key, layout.wire_coefficients[0], n + 2);
                proof.b_commitment = commit_polynomial(
                    layout, key, layout.wire_coefficients[1], n + 2);
                proof.c_commitment = commit_polynomial(
                    layout, key, layout.wire_coefficients[2], n + 2);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round1.transcript");
                transcript.append(proof.a_commitment);
                transcript.append(proof.b_commitment);
                transcript.append(proof.c_commitment);
                beta = transcript.challenge();
                gamma = transcript.challenge();
            }
        }

        Scalar alpha = Scalar::zero(), alpha_squared = Scalar::zero();
        {
            ProfileScope round("plonk.cuda.prove.round2.total");
            {
                ProfileScope stage("plonk.cuda.prove.round2.permutation_products");
                constexpr std::size_t block_size = 256;
                permutation_products_kernel<<<ceil_div(n, block_size), block_size>>>(
                    layout.wire_evaluations[0], layout.wire_evaluations[1],
                    layout.wire_evaluations[2], layout.omegas,
                    layout.fixed_evaluations[SIGMA1],
                    layout.fixed_evaluations[SIGMA2],
                    layout.fixed_evaluations[SIGMA3], device_value<fr_t>(beta),
                    device_value<fr_t>(gamma), device_value<fr_t>(key.domain.k_1),
                    device_value<fr_t>(key.domain.k_2),
                    layout.permutation_numerator,
                    layout.permutation_denominator, n);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round2.pointwise_inverse");
                pointwise_inverse(layout.permutation_denominator, n);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round2.grand_product");
                constexpr std::size_t block_size = 256;
                kernel<<<ceil_div(n, block_size), block_size>>>(
                    [] __device__(fr_t *numerator, const fr_t *inverse,
                                  std::size_t count) {
                        const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                        if (i < count) numerator[i] *= inverse[i];
                    }, layout.permutation_numerator,
                    layout.permutation_denominator, n);
                layout.product_scan.inclusive(
                    layout.permutation_numerator,
                    layout.permutation_numerator, n, FieldMultiply<fr_t>{},
                    device_value<fr_t>(Scalar::one()));
                Scalar accumulator;
                CUDA_CHECK(cudaMemcpy(&accumulator,
                                      layout.permutation_numerator + n - 1,
                                      sizeof(fr_t), cudaMemcpyDeviceToHost));
                if (accumulator != Scalar::one())
                    throw std::invalid_argument(
                        "PLONK Prove: witness violates the copy permutation");
                kernel<<<ceil_div(n, block_size), block_size>>>(
                    [] __device__(const fr_t *prefix, fr_t *z,
                                  std::size_t count) {
                        const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                        if (i >= count) return;
                        z[i] = i == 0 ? fr_t::one() : prefix[i - 1];
                    }, layout.permutation_numerator, layout.z_evaluations, n);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round2.z_intt_blinding");
                CUDA_CHECK(cudaMemcpy(layout.z_coefficients, layout.z_evaluations,
                                      n * sizeof(fr_t), cudaMemcpyDeviceToDevice));
                layout.domain_ntt.intt(layout.z_coefficients);
                const Scalar quadratic = Scalar::random_element();
                const Scalar linear = Scalar::random_element();
                const Scalar constant = Scalar::random_element();
                kernel<<<1, 1>>>([] __device__(fr_t *coefficients,
                                               std::size_t size,
                                               fr_t quadratic,
                                               fr_t linear,
                                               fr_t constant) {
                    coefficients[0] -= constant;
                    coefficients[1] -= linear;
                    coefficients[2] -= quadratic;
                    coefficients[size] += constant;
                    coefficients[size + 1] += linear;
                    coefficients[size + 2] += quadratic;
                }, layout.z_coefficients, n, device_value<fr_t>(quadratic),
                    device_value<fr_t>(linear), device_value<fr_t>(constant));
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round2.z_msm");
                proof.z_commitment = commit_polynomial(
                    layout, key, layout.z_coefficients, n + 3);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round2.transcript");
                transcript.append(proof.z_commitment);
                alpha = transcript.challenge();
                alpha_squared = alpha.squared();
            }
        }

        const Scalar n_inverse = Scalar(static_cast<long>(n)).inverse();
        Scalar zeta = Scalar::zero(), zeta_omega = Scalar::zero();
        Scalar z_h_zeta = Scalar::zero();
        {
            ProfileScope round("plonk.cuda.prove.round3.total");
            {
                ProfileScope stage("plonk.cuda.prove.round3.public_input_intt");
                layout.domain_ntt.intt(layout.public_input_coefficients);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round3.coset_ntt");
                for (std::size_t wire = 0; wire < 3; ++wire)
                    copy_to_coset(layout.wire_coset_evaluations[wire],
                                  layout.wire_coefficients[wire], n + 2, layout);
                copy_to_coset(layout.z_coset_evaluations,
                              layout.z_coefficients, n + 3, layout);
                copy_to_coset(layout.public_input_coset_evaluations,
                              layout.public_input_coefficients, n, layout);
            }
            {
                ProfileScope stage(
                    "plonk.cuda.prove.round3.quotient_domain_evaluations");
                const Scalar quotient_omega =
                    reinterpret_cast<const Scalar *>(forward_roots_of_unity)
                        [libff::log2(quotient_size)];
                CUDA_CHECK(cudaMemcpy(layout.coset_points + 1, &quotient_omega,
                                      sizeof(fr_t), cudaMemcpyHostToDevice));
                power_computing<<<1, 1>>>(layout.coset_points, quotient_size);
                constexpr std::size_t block_size = 256;
                const unsigned log_n = libff::log2(n);
                kernel<<<ceil_div(quotient_size, block_size), block_size>>>(
                    [] __device__(fr_t *points, fr_t *z_h,
                                  fr_t *denominators, fr_t shift,
                                  unsigned log_domain_size,
                                  std::size_t count) {
                        const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                        if (i >= count) return;
                        const fr_t point = points[i] * shift;
                        points[i] = point;
                        fr_t point_to_n = point;
                        for (unsigned round = 0; round < log_domain_size; ++round)
                            point_to_n *= point_to_n;
                        const fr_t vanishing = point_to_n - fr_t::one();
                        z_h[i] = vanishing;
                        denominators[i] = vanishing;
                        denominators[count + i] = point - fr_t::one();
                    }, layout.coset_points, layout.z_h_evaluations,
                    layout.quotient_denominators,
                    device_value<fr_t>(key.domain.coset_shift), log_n,
                    quotient_size);
                pointwise_inverse(layout.quotient_denominators,
                                  2 * quotient_size);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round3.quotient_evaluations");
                constexpr std::size_t block_size = 256;
                const std::size_t omega_shift = quotient_size / n;
                kernel<<<ceil_div(quotient_size, block_size), block_size>>>(
                    [] __device__(const fr_t *a, const fr_t *b, const fr_t *c,
                                  const fr_t *z, const fr_t *pi,
                                  const fr_t *q_l, const fr_t *q_r,
                                  const fr_t *q_m, const fr_t *q_o,
                                  const fr_t *q_c, const fr_t *sigma_1,
                                  const fr_t *sigma_2, const fr_t *sigma_3,
                                  const fr_t *points, const fr_t *z_h,
                                  const fr_t *inverse_denominators,
                                  fr_t beta, fr_t gamma, fr_t alpha,
                                  fr_t alpha_squared, fr_t k_1, fr_t k_2,
                                  fr_t n_inverse, std::size_t rotation,
                                  fr_t *quotient, std::size_t count) {
                        const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                        if (i >= count) return;
                        const fr_t point = points[i];
                        const fr_t l_1 = z_h[i] * n_inverse *
                            inverse_denominators[count + i];
                        const fr_t gate = a[i] * q_l[i] + b[i] * q_r[i] +
                            a[i] * b[i] * q_m[i] + c[i] * q_o[i] + q_c[i] + pi[i];
                        const fr_t identity =
                            (a[i] + beta * point + gamma) *
                            (b[i] + beta * k_1 * point + gamma) *
                            (c[i] + beta * k_2 * point + gamma);
                        const fr_t sigma =
                            (a[i] + beta * sigma_1[i] + gamma) *
                            (b[i] + beta * sigma_2[i] + gamma) *
                            (c[i] + beta * sigma_3[i] + gamma);
                        const fr_t permutation = z[i] * identity -
                            z[(i + rotation) % count] * sigma;
                        quotient[i] =
                            (gate + alpha * permutation +
                             alpha_squared * (z[i] - fr_t::one()) * l_1) *
                            inverse_denominators[i];
                    }, layout.wire_coset_evaluations[0],
                    layout.wire_coset_evaluations[1],
                    layout.wire_coset_evaluations[2],
                    layout.z_coset_evaluations,
                    layout.public_input_coset_evaluations,
                    layout.fixed_coset_evaluations[QL],
                    layout.fixed_coset_evaluations[QR],
                    layout.fixed_coset_evaluations[QM],
                    layout.fixed_coset_evaluations[QO],
                    layout.fixed_coset_evaluations[QC],
                    layout.fixed_coset_evaluations[SIGMA1],
                    layout.fixed_coset_evaluations[SIGMA2],
                    layout.fixed_coset_evaluations[SIGMA3], layout.coset_points,
                    layout.z_h_evaluations, layout.quotient_denominators,
                    device_value<fr_t>(beta), device_value<fr_t>(gamma),
                    device_value<fr_t>(alpha),
                    device_value<fr_t>(alpha_squared),
                    device_value<fr_t>(key.domain.k_1),
                    device_value<fr_t>(key.domain.k_2),
                    device_value<fr_t>(n_inverse), omega_shift,
                    layout.quotient_evaluations, quotient_size);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round3.quotient_icntt");
                layout.quotient_ntt.coset_intt(layout.quotient_evaluations);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round3.split_blinding");
                for (std::size_t part = 0; part < 3; ++part) {
                    CUDA_CHECK(cudaMemset(layout.t_parts[part], 0,
                                          (n + 3) * sizeof(fr_t)));
                    CUDA_CHECK(cudaMemcpy(
                        layout.t_parts[part],
                        layout.quotient_evaluations + part * part_size,
                        part_size * sizeof(fr_t), cudaMemcpyDeviceToDevice));
                }
                const Scalar first = Scalar::random_element();
                const Scalar second = Scalar::random_element();
                kernel<<<1, 1>>>([] __device__(fr_t *lo, fr_t *mid, fr_t *hi,
                                               std::size_t size,
                                               fr_t first, fr_t second) {
                    lo[size] += first;
                    mid[0] -= first;
                    mid[size] += second;
                    hi[0] -= second;
                }, layout.t_parts[0], layout.t_parts[1], layout.t_parts[2],
                    part_size, device_value<fr_t>(first),
                    device_value<fr_t>(second));
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round3.quotient_msm");
                proof.t_lo_commitment = commit_polynomial(
                    layout, key, layout.t_parts[0], n + 3);
                proof.t_mid_commitment = commit_polynomial(
                    layout, key, layout.t_parts[1], n + 3);
                proof.t_hi_commitment = commit_polynomial(
                    layout, key, layout.t_parts[2], n + 3);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round3.transcript");
                transcript.append(proof.t_lo_commitment);
                transcript.append(proof.t_mid_commitment);
                transcript.append(proof.t_hi_commitment);
                zeta = transcript.challenge();
                zeta_omega = zeta * key.domain.omega;
                z_h_zeta = (zeta ^ n) - Scalar::one();
                if (z_h_zeta.is_zero())
                    throw std::runtime_error(
                        "PLONK Prove: transcript challenge lies in H");
            }
        }

        Scalar batching_challenge = Scalar::zero();
        {
            ProfileScope round("plonk.cuda.prove.round4.total");
            {
                ProfileScope stage(
                    "plonk.cuda.prove.round4.polynomial_evaluations");
                proof.a_zeta = layout.polynomial_evaluation.evaluate(
                    layout.wire_coefficients[0], n + 2, zeta);
                proof.b_zeta = layout.polynomial_evaluation.evaluate(
                    layout.wire_coefficients[1], n + 2, zeta);
                proof.c_zeta = layout.polynomial_evaluation.evaluate(
                    layout.wire_coefficients[2], n + 2, zeta);
                proof.sigma_1_zeta = layout.polynomial_evaluation.evaluate(
                    layout.fixed_coefficients[SIGMA1], n, zeta);
                proof.sigma_2_zeta = layout.polynomial_evaluation.evaluate(
                    layout.fixed_coefficients[SIGMA2], n, zeta);
                proof.z_zeta_omega = layout.polynomial_evaluation.evaluate(
                    layout.z_coefficients, n + 3, zeta_omega);
            }
            {
                ProfileScope stage("plonk.cuda.prove.round4.transcript");
                transcript.append(proof.a_zeta);
                transcript.append(proof.b_zeta);
                transcript.append(proof.c_zeta);
                transcript.append(proof.sigma_1_zeta);
                transcript.append(proof.sigma_2_zeta);
                transcript.append(proof.z_zeta_omega);
                batching_challenge = transcript.challenge();
            }
        }

        {
            ProfileScope round("plonk.cuda.prove.round5.total");
            {
                ProfileScope stage("plonk.cuda.prove.round5.linearization");
                const Scalar identity_at_zeta =
                    (proof.a_zeta + beta * zeta + gamma) *
                    (proof.b_zeta + beta * key.domain.k_1 * zeta + gamma) *
                    (proof.c_zeta + beta * key.domain.k_2 * zeta + gamma);
                const Scalar sigma_12_at_zeta =
                    (proof.a_zeta + beta * proof.sigma_1_zeta + gamma) *
                    (proof.b_zeta + beta * proof.sigma_2_zeta + gamma);
                const Scalar l_1_zeta = z_h_zeta * n_inverse *
                    (zeta - Scalar::one()).inverse();
                const Scalar z_scale =
                    alpha * identity_at_zeta + alpha_squared * l_1_zeta;
                const Scalar sigma_3_scale =
                    -alpha * sigma_12_at_zeta * beta * proof.z_zeta_omega;
                const Scalar zeta_to_part = zeta ^ part_size;
                const Scalar zeta_to_two_parts = zeta_to_part.squared();
                constexpr std::size_t block_size = 256;
                kernel<<<ceil_div(n + 3, block_size), block_size>>>(
                    [] __device__(const fr_t *q_l, const fr_t *q_r,
                                  const fr_t *q_m, const fr_t *q_o,
                                  const fr_t *q_c, const fr_t *sigma_3,
                                  const fr_t *z, const fr_t *t_lo,
                                  const fr_t *t_mid, const fr_t *t_hi,
                                  fr_t a_zeta, fr_t b_zeta, fr_t c_zeta,
                                  fr_t z_scale, fr_t sigma_3_scale,
                                  fr_t zeta_to_part, fr_t zeta_to_two_parts,
                                  fr_t *linearization, fr_t *t_partial,
                                  std::size_t domain_size) {
                        const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                        if (i >= domain_size + 3) return;
                        fr_t linear = z[i] * z_scale;
                        if (i < domain_size) {
                            linear += q_l[i] * a_zeta;
                            linear += q_r[i] * b_zeta;
                            linear += q_m[i] * a_zeta * b_zeta;
                            linear += q_o[i] * c_zeta;
                            linear += q_c[i];
                            linear += sigma_3[i] * sigma_3_scale;
                        }
                        linearization[i] = linear;
                        t_partial[i] = t_lo[i] + zeta_to_part * t_mid[i] +
                            zeta_to_two_parts * t_hi[i];
                    }, layout.fixed_coefficients[QL],
                    layout.fixed_coefficients[QR],
                    layout.fixed_coefficients[QM],
                    layout.fixed_coefficients[QO],
                    layout.fixed_coefficients[QC],
                    layout.fixed_coefficients[SIGMA3], layout.z_coefficients,
                    layout.t_parts[0], layout.t_parts[1], layout.t_parts[2],
                    device_value<fr_t>(proof.a_zeta),
                    device_value<fr_t>(proof.b_zeta),
                    device_value<fr_t>(proof.c_zeta),
                    device_value<fr_t>(z_scale),
                    device_value<fr_t>(sigma_3_scale),
                    device_value<fr_t>(zeta_to_part),
                    device_value<fr_t>(zeta_to_two_parts),
                    layout.linearization, layout.t_partial, n);
                proof.p_non_constant_zeta =
                    layout.polynomial_evaluation.evaluate(
                        layout.linearization, n + 3, zeta);
                proof.t_zeta = layout.polynomial_evaluation.evaluate(
                    layout.t_partial, n + 3, zeta);
            }

            {
                ProfileScope stage("plonk.cuda.prove.round5.batch_opening");
                std::array<Scalar, 7> powers;
                powers[0] = Scalar::one();
                for (std::size_t i = 1; i < powers.size(); ++i)
                    powers[i] = powers[i - 1] * batching_challenge;
                CUDA_CHECK(cudaMemcpy(layout.batching_powers, powers.data(),
                                      powers.size() * sizeof(fr_t),
                                      cudaMemcpyHostToDevice));
                constexpr std::size_t block_size = 256;
                kernel<<<ceil_div(n + 3, block_size), block_size>>>(
                    [] __device__(const fr_t *t, const fr_t *p,
                                  const fr_t *a, const fr_t *b, const fr_t *c,
                                  const fr_t *sigma_1, const fr_t *sigma_2,
                                  const fr_t *powers, fr_t *result,
                                  std::size_t domain_size) {
                        const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
                        if (i >= domain_size + 3) return;
                        fr_t value = t[i] + powers[1] * p[i] +
                            powers[2] * a[i] + powers[3] * b[i] +
                            powers[4] * c[i];
                        if (i < domain_size)
                            value += powers[5] * sigma_1[i] +
                                     powers[6] * sigma_2[i];
                        result[i] = value;
                    }, layout.t_partial, layout.linearization,
                    layout.wire_coefficients[0], layout.wire_coefficients[1],
                    layout.wire_coefficients[2],
                    layout.fixed_coefficients[SIGMA1],
                    layout.fixed_coefficients[SIGMA2], layout.batching_powers,
                    layout.batched_polynomial, n);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round5.synthetic_division");
                layout.synthetic_division.divide(
                    layout.batched_polynomial, n + 3, zeta,
                    layout.opening_polynomials[0]);
                layout.synthetic_division.divide(
                    layout.z_coefficients, n + 3, zeta_omega,
                    layout.opening_polynomials[1]);
                profile_synchronize();
            }
            {
                ProfileScope stage("plonk.cuda.prove.round5.opening_msm");
                proof.w_zeta = commit_polynomial(
                    layout, key, layout.opening_polynomials[0], n + 2);
                proof.w_zeta_omega = commit_polynomial(
                    layout, key, layout.opening_polynomials[1], n + 2);
            }
        }
    }
};

} // namespace

std::unique_ptr<Backend> make_cuda_backend()
{
    return std::make_unique<CudaBackend>();
}

} // namespace nkr::plonk
