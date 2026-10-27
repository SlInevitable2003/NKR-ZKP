#include "plonk/backend.hpp"

#include <stdexcept>

namespace nkr::plonk {

std::vector<std::string> available_backends()
{
    std::vector<std::string> result{"cpu"};
#if defined(NKR_PLONK_ACCELERATOR_CUDA)
    result.emplace_back("cuda");
#elif defined(NKR_PLONK_ACCELERATOR_HIP)
    result.emplace_back("hip");
#endif
    return result;
}

std::unique_ptr<Backend> make_backend(const std::string &name)
{
    if (name == "cpu") return make_cpu_backend();
#if defined(NKR_PLONK_ACCELERATOR_CUDA)
    if (name == "cuda") return make_cuda_backend();
#elif defined(NKR_PLONK_ACCELERATOR_HIP)
    if (name == "hip" || name == "dcu") return make_hip_backend();
#endif
    throw std::invalid_argument("PLONK backend is not available in this build: " + name);
}

} // namespace nkr::plonk
