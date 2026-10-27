#include "plonk/backend.hpp"
#include "plonk/protocol.hpp"

#include <memory>

#include "nkr/operators/cuda.hpp"

namespace nkr::plonk {
namespace {

class CudaBackend final : public Backend {
public:
    const char *name() const override { return "cuda"; }
    void setup(const Circuit &, const UniversalSrs &, ProvingKey &, VerificationKey &) override
    {
        throw_scaffold_only();
    }
    void prove(const ProvingKey &, const Witness &, Proof &) override
    {
        throw_scaffold_only();
    }
};

} // namespace

std::unique_ptr<Backend> make_cuda_backend()
{
    return std::make_unique<CudaBackend>();
}

} // namespace nkr::plonk
