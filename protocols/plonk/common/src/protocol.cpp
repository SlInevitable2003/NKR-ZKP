#include "plonk/protocol.hpp"

#include <stdexcept>

namespace nkr::plonk {

[[noreturn]] void throw_scaffold_only()
{
    throw std::logic_error(
        "PLONK is a structural scaffold; protocol semantics are not implemented yet");
}

void setup(Backend &backend,
           const Circuit &circuit,
           const UniversalSrs &srs,
           ProvingKey &proving_key,
           VerificationKey &verification_key)
{
    backend.setup(circuit, srs, proving_key, verification_key);
}

Proof prove(Backend &backend,
            const ProvingKey &proving_key,
            const Witness &witness)
{
    Proof proof;
    backend.prove(proving_key, witness, proof);
    return proof;
}

bool verify(const VerificationKey &,
            const std::vector<Scalar> &,
            const Proof &)
{
    throw_scaffold_only();
}

} // namespace nkr::plonk
