cmake_minimum_required(VERSION 3.21)

project(NKR_ZKP LANGUAGES CXX)

set(NKR_ZKP_ACCELERATOR "NONE" CACHE STRING
    "Optional accelerator to build alongside CPU: NONE, CUDA, or HIP")
set_property(CACHE NKR_ZKP_ACCELERATOR PROPERTY STRINGS NONE CUDA HIP)
string(TOUPPER "${NKR_ZKP_ACCELERATOR}" NKR_ZKP_ACCELERATOR)

set(NKR_ZKP_PROTOCOL "ALL" CACHE STRING
    "Protocol to configure: ALL, GROTH16, or PLONK")
set_property(CACHE NKR_ZKP_PROTOCOL PROPERTY STRINGS ALL GROTH16 PLONK)
string(TOUPPER "${NKR_ZKP_PROTOCOL}" NKR_ZKP_PROTOCOL)
if(NOT NKR_ZKP_PROTOCOL MATCHES "^(ALL|GROTH16|PLONK)$")
    message(FATAL_ERROR "Unsupported protocol: ${NKR_ZKP_PROTOCOL}")
endif()

set(NKR_ZKP_LIGHTZK_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lightzk/depends"
    CACHE PATH "Path to the vendored lightZK dependencies")

find_package(OpenMP REQUIRED)

# Keep libff/libfqfft single-threaded.  Protocol CPU benchmarks rely on their
# Prove paths being single-threaded; Setup parallelism is introduced explicitly
# at the operator call sites instead of through libff's global MULTICORE switch.
set(MULTICORE OFF CACHE BOOL "Disable dependency-wide OpenMP parallelism" FORCE)

if(NOT IS_DIRECTORY "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark")
    message(FATAL_ERROR "lightZK dependencies not found at ${NKR_ZKP_LIGHTZK_DEPS_DIR}")
endif()

# libff's optimized assembly is specific to x86-64. Seed its existing
# USE_ASM option according to the target architecture while preserving an
# explicit value supplied by the caller.
if(NOT DEFINED USE_ASM)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(USE_ASM ON CACHE BOOL "Use libff architecture-specific assembly")
    else()
        set(USE_ASM OFF CACHE BOOL "Use libff architecture-specific assembly")
    endif()
endif()
message(STATUS "libff architecture-specific assembly: ${USE_ASM} (${CMAKE_SYSTEM_PROCESSOR})")

# Both Groth16 and PLONK use the same vendored BN254/libsnark dependency
# snapshot. Configure it at the repository root so neither protocol owns the
# other's CPU dependency.
set(CURVE "ALT_BN128" CACHE STRING "libsnark curve used by NKR-ZKP" FORCE)
set(DEPENDS_DIR "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark/depends" CACHE PATH
    "Vendored libsnark dependency directory" FORCE)
set(WITH_SUPERCOP OFF CACHE BOOL "ADSNARK/SUPERCOP is not vendored" FORCE)
set(WITH_PROCPS OFF CACHE BOOL "Disable optional libsnark memory profiling" FORCE)
set(NKR_ZKP_BUILD_LIBSNARK_AUXILIARY OFF CACHE BOOL
    "Vendored libsnark demos, profiles, and upstream tests are not shipped" FORCE)

if(NKR_ZKP_ACCELERATOR STREQUAL "CUDA")
    enable_language(CUDA)
    if(NOT CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 120)
    endif()
elseif(NKR_ZKP_ACCELERATOR STREQUAL "HIP")
    enable_language(HIP)
elseif(NOT NKR_ZKP_ACCELERATOR STREQUAL "NONE")
    message(FATAL_ERROR "Unsupported accelerator: ${NKR_ZKP_ACCELERATOR}")
endif()

add_subdirectory(
    "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark"
    "${CMAKE_BINARY_DIR}/third_party/lightzk/libsnark")

add_subdirectory(common)
add_subdirectory(operators)
if(NKR_ZKP_ACCELERATOR STREQUAL "CUDA")
    add_subdirectory(operators/cuda)
elseif(NKR_ZKP_ACCELERATOR STREQUAL "HIP")
    add_subdirectory(operators/hip)
endif()

if(NKR_ZKP_PROTOCOL STREQUAL "ALL" OR NKR_ZKP_PROTOCOL STREQUAL "GROTH16")
    add_subdirectory(protocols/groth16)
endif()
if(NKR_ZKP_PROTOCOL STREQUAL "ALL" OR NKR_ZKP_PROTOCOL STREQUAL "PLONK")
    add_subdirectory(protocols/plonk)
endif()
