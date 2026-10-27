#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "plonk/types.hpp"

namespace nkr::plonk {

// SHA3-256 Fiat-Shamir transcript following Lambdaworks' documented strategy:
// append places the newest message before the prior state, while challenge
// hashes the accumulated state and replaces it with the resulting digest.
class Transcript {
public:
    void append(const void *data, std::size_t size);
    void append_label(std::string_view label);
    void append(std::uint64_t value);
    void append(const Scalar &value);
    void append(const G1 &point);

    Scalar challenge();

private:
    std::vector<std::vector<std::uint8_t>> messages_;
};

Transcript initialize_transcript(const VerificationKey &key,
                                 const std::vector<Scalar> &public_input);

} // namespace nkr::plonk
