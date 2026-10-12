#include "groth16/backend.hpp"

#include <stdexcept>

namespace nkr::groth16 {

std::vector<std::string> available_backends()
{
    std::vector<std::string> result{"cpu"};
#if defined(NKR_GROTH16_ACCELERATOR_CUDA)
    result.emplace_back("cuda");
#elif defined(NKR_GROTH16_ACCELERATOR_DCU)
    result.emplace_back("dcu");
#endif
    return result;
}

std::unique_ptr<Backend> make_backend(const std::string &name)
{
    if (name == "cpu") return make_cpu_backend();
#if defined(NKR_GROTH16_ACCELERATOR_CUDA)
    if (name == "cuda") return make_cuda_backend();
#elif defined(NKR_GROTH16_ACCELERATOR_DCU)
    if (name == "dcu") return make_dcu_backend();
#endif
    throw std::invalid_argument("Groth16 backend is not available in this build: " + name);
}

} // namespace nkr::groth16

