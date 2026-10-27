#include "plonk/transcript.hpp"

#include <array>
#include <stdexcept>

#include <openssl/evp.h>

namespace nkr::plonk {
namespace {

template <typename Field>
std::vector<std::uint8_t> encode_field(const Field &value)
{
    constexpr std::size_t encoded_size = 32;
    const auto integer = value.as_bigint();
    std::vector<std::uint8_t> encoded(encoded_size, 0);
    for (std::size_t byte = 0; byte < encoded_size; ++byte) {
        std::uint8_t result = 0;
        const std::size_t first_bit = 8 * (encoded_size - 1 - byte);
        for (std::size_t bit = 0; bit < 8; ++bit) {
            if (integer.test_bit(first_bit + bit)) result |= std::uint8_t{1} << bit;
        }
        encoded[byte] = result;
    }
    return encoded;
}

} // namespace

void Transcript::append(const void *data, const std::size_t size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    messages_.emplace_back(bytes, bytes + size);
}

void Transcript::append_label(const std::string_view label)
{
    append(label.data(), label.size());
}

void Transcript::append(const std::uint64_t value)
{
    std::array<std::uint8_t, 8> encoded{};
    for (std::size_t i = 0; i < encoded.size(); ++i)
        encoded[i] = static_cast<std::uint8_t>(value >> (8 * (encoded.size() - 1 - i)));
    append(encoded.data(), encoded.size());
}

void Transcript::append(const Scalar &value)
{
    const std::vector<std::uint8_t> encoded = encode_field(value);
    append(encoded.data(), encoded.size());
}

void Transcript::append(const G1 &point)
{
    G1 affine = point;
    affine.to_affine_coordinates();
    const std::vector<std::uint8_t> x = encode_field(affine.X);
    const std::vector<std::uint8_t> y = encode_field(affine.Y);
    std::vector<std::uint8_t> encoded;
    encoded.reserve(1 + x.size() + y.size());
    encoded.push_back(affine.is_zero() ? 1 : 0);
    encoded.insert(encoded.end(), x.begin(), x.end());
    encoded.insert(encoded.end(), y.begin(), y.end());
    append(encoded.data(), encoded.size());
}

Scalar Transcript::challenge()
{
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!context) throw std::runtime_error("PLONK transcript: cannot allocate SHA3 context");

    std::array<std::uint8_t, 32> digest{};
    unsigned int digest_size = 0;
    bool ok = EVP_DigestInit_ex(context, EVP_sha3_256(), nullptr) == 1;
    for (auto it = messages_.rbegin(); ok && it != messages_.rend(); ++it)
        ok = EVP_DigestUpdate(context, it->data(), it->size()) == 1;
    ok = ok && EVP_DigestFinal_ex(context, digest.data(), &digest_size) == 1;
    EVP_MD_CTX_free(context);
    if (!ok || digest_size != digest.size())
        throw std::runtime_error("PLONK transcript: SHA3-256 failed");

    messages_.clear();
    messages_.emplace_back(digest.begin(), digest.end());

    Scalar result = Scalar::zero();
    const Scalar radix(256);
    for (const std::uint8_t byte : digest) result = result * radix + Scalar(byte);
    return result;
}

Transcript initialize_transcript(const VerificationKey &key,
                                 const std::vector<Scalar> &public_input)
{
    Transcript transcript;
    transcript.append_label("NKR-ZKP-PLONK-v1");
    transcript.append(static_cast<std::uint64_t>(key.domain.domain_size));
    transcript.append(key.commitments.q_l);
    transcript.append(key.commitments.q_r);
    transcript.append(key.commitments.q_m);
    transcript.append(key.commitments.q_o);
    transcript.append(key.commitments.q_c);
    transcript.append(key.commitments.sigma_1);
    transcript.append(key.commitments.sigma_2);
    transcript.append(key.commitments.sigma_3);
    for (const Scalar &value : public_input) transcript.append(value);
    return transcript;
}

} // namespace nkr::plonk
