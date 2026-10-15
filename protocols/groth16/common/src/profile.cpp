#include "groth16/profile.hpp"

#include <iomanip>
#include <iostream>
#include <utility>

#include "libff/common/profiling.hpp"

namespace nkr::groth16 {
namespace {

bool profile_enabled = false;

} // namespace

void configure_profiling(bool enabled)
{
    profile_enabled = enabled;
    libff::inhibit_profiling_info = !enabled;
    if (enabled) libff::start_profiling();
}

bool profiling_enabled()
{
    return profile_enabled;
}

ProfileScope::ProfileScope(std::string label)
    : enabled_(profiling_enabled()), label_(std::move(label)), start_(std::chrono::steady_clock::now())
{}

ProfileScope::~ProfileScope()
{
    if (!enabled_) return;
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_);
    std::cout << "[profile] " << label_ << " = " << std::fixed << std::setprecision(3)
              << elapsed.count() << " ms\n";
}

} // namespace nkr::groth16
