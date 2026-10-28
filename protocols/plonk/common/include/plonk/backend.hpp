#pragma once

#include <memory>
#include <string>
#include <vector>

#include "plonk/types.hpp"

namespace nkr::plonk {

class Backend {
public:
    virtual ~Backend() = default;
    virtual const char *name() const = 0;

    virtual void setup(
        const Circuit &circuit,
        const UniversalSrs &srs,
        ProvingKey &proving_key,
        VerificationKey &verification_key) = 0;

    virtual void prove(
        const ProvingKey &proving_key,
        const Witness &witness,
        Proof &proof) = 0;
};

std::unique_ptr<Backend> make_cpu_backend();

#if defined(NKR_PLONK_ACCELERATOR_CUDA)
std::unique_ptr<Backend> make_cuda_backend();
#elif defined(NKR_PLONK_ACCELERATOR_HIP)
std::unique_ptr<Backend> make_hip_backend();
#endif

std::unique_ptr<Backend> make_backend(const std::string &name);
std::vector<std::string> available_backends();

} // namespace nkr::plonk
