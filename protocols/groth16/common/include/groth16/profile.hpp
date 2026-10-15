#pragma once

#include <chrono>
#include <string>

namespace nkr::groth16 {

void configure_profiling(bool enabled);
bool profiling_enabled();

class ProfileScope {
public:
    explicit ProfileScope(std::string label);
    ~ProfileScope();

    ProfileScope(const ProfileScope &) = delete;
    ProfileScope &operator=(const ProfileScope &) = delete;

private:
    bool enabled_;
    std::string label_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace nkr::groth16
