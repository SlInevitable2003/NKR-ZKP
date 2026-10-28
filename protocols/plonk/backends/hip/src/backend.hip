#include "plonk/backend.hpp"

#include <memory>
#include <stdexcept>

#include "nkr/operators/hip.hpp"

namespace nkr::plonk {
namespace {

class HipBackend final : public Backend {
public:
    const char *name() const override { return "hip"; }
    void setup(const Circuit &, const UniversalSrs &, ProvingKey &, VerificationKey &) override
    {
        throw std::logic_error("the requested PLONK HIP operation is not implemented yet");
    }
    void prove(const ProvingKey &, const Witness &, Proof &) override
    {
        throw std::logic_error("the requested PLONK HIP operation is not implemented yet");
    }
};

} // namespace

std::unique_ptr<Backend> make_hip_backend()
{
    return std::make_unique<HipBackend>();
}

} // namespace nkr::plonk
