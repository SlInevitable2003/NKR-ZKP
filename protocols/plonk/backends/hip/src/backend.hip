#include "plonk/backend.hpp"
#include "plonk/protocol.hpp"

#include <memory>

#include "nkr/operators/hip.hpp"

namespace nkr::plonk {
namespace {

class HipBackend final : public Backend {
public:
    const char *name() const override { return "hip"; }
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

std::unique_ptr<Backend> make_hip_backend()
{
    return std::make_unique<HipBackend>();
}

} // namespace nkr::plonk
