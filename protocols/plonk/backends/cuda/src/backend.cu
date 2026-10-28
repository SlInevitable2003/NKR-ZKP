#include "plonk/backend.hpp"

#include <memory>
#include <stdexcept>

#include "nkr/operators/cuda.hpp"

namespace nkr::plonk {
namespace {

class CudaBackend final : public Backend {
public:
    const char *name() const override { return "cuda"; }
    void setup(const Circuit &, const UniversalSrs &, ProvingKey &, VerificationKey &) override
    {
        throw std::logic_error("the requested PLONK CUDA operation is not implemented yet");
    }
    void prove(const ProvingKey &, const Witness &, Proof &) override
    {
        throw std::logic_error("the requested PLONK CUDA operation is not implemented yet");
    }
};

} // namespace

std::unique_ptr<Backend> make_cuda_backend()
{
    return std::make_unique<CudaBackend>();
}

} // namespace nkr::plonk
