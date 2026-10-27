#pragma once

#include <memory>
#include <string>
#include <vector>

#include "groth16/types.hpp"

namespace nkr::groth16 {

class Backend {
public:
    virtual ~Backend() = default;
    virtual const char *name() const = 0;

    virtual void setup(
        const Circuit &circuit,
        ProvingKey &proving_key,
        VerificationKey &verification_key) = 0;

    virtual void prove(
        const Circuit &circuit,
        const ProvingKey &proving_key,
        const std::vector<Scalar> &witness,
        RawProof &proof) = 0;
};

std::unique_ptr<Backend> make_cpu_backend();

#if defined(NKR_GROTH16_ACCELERATOR_CUDA)
std::unique_ptr<Backend> make_cuda_backend();
#elif defined(NKR_GROTH16_ACCELERATOR_HIP)
std::unique_ptr<Backend> make_hip_backend();
#endif

std::unique_ptr<Backend> make_backend(const std::string &name);
std::vector<std::string> available_backends();

} // namespace nkr::groth16
