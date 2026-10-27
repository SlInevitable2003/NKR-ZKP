#include "plonk/backend.hpp"
#include "plonk/protocol.hpp"

#include <memory>

namespace nkr::plonk {
namespace {

class CpuBackend final : public Backend {
public:
    const char *name() const override { return "cpu"; }

    void setup(const Circuit &,
               const UniversalSrs &,
               ProvingKey &,
               VerificationKey &) override
    {
        throw_scaffold_only();
    }

    void prove(const ProvingKey &,
               const Witness &,
               Proof &) override
    {
        throw_scaffold_only();
    }
};

} // namespace

std::unique_ptr<Backend> make_cpu_backend()
{
    return std::make_unique<CpuBackend>();
}

} // namespace nkr::plonk
