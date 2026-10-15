set(NKR_ZKP_GROTH16_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/depends" CACHE PATH
    "Path to the vendored lightZK depends directory containing libsnark and cgbn")
if(NOT IS_DIRECTORY "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark")
    message(FATAL_ERROR "Groth16 dependencies not found at ${NKR_ZKP_GROTH16_DEPS_DIR}")
endif()

set(NKR_ZKP_GROTH16_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data" CACHE PATH
    "Directory for Groth16 test artifacts")
option(NKR_ZKP_GROTH16_BUILD_LEGACY_TESTS "Build imported backend microbenchmarks" OFF)

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

# The vendored dependency snapshot is deliberately limited to the framework's
# ALT_BN128 production path. Force these values so pre-cleanup CMake caches
# cannot re-enable source trees that are no longer shipped.
set(CURVE "ALT_BN128" CACHE STRING "libsnark curve used by NKR-ZKP" FORCE)
set(DEPENDS_DIR "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark/depends" CACHE PATH
    "Vendored libsnark dependency directory" FORCE)
set(WITH_SUPERCOP OFF CACHE BOOL "ADSNARK/SUPERCOP is not vendored" FORCE)
set(WITH_PROCPS OFF CACHE BOOL "Disable optional libsnark memory profiling" FORCE)
set(NKR_ZKP_BUILD_LIBSNARK_AUXILIARY OFF CACHE BOOL
    "Vendored libsnark demos, profiles, and upstream tests are not shipped" FORCE)

find_package(OpenMP REQUIRED)
add_subdirectory(
    "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark"
    "${CMAKE_CURRENT_BINARY_DIR}/depends/libsnark")

add_library(nkr_groth16_common STATIC
    common/src/backend_registry.cpp
    common/src/io.cpp
    common/src/profile.cpp
    common/src/protocol.cpp)
target_compile_features(nkr_groth16_common PUBLIC cxx_std_17)
target_include_directories(nkr_groth16_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include"
    "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark")
target_compile_definitions(nkr_groth16_common PUBLIC CURVE_ALT_BN128)
target_link_libraries(nkr_groth16_common PUBLIC snark OpenMP::OpenMP_CXX)

add_library(nkr_groth16_cpu STATIC backends/cpu/src/backend.cpp)
target_link_libraries(nkr_groth16_cpu PUBLIC nkr_groth16_common)

set(NKR_ZKP_GROTH16_BACKEND_TARGET "")
if(NKR_ZKP_GROTH16_BACKEND STREQUAL "CUDA")
    enable_language(CUDA)
    if(NOT CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 120)
    endif()
    add_subdirectory(backends/cuda)
    set(NKR_ZKP_GROTH16_BACKEND_TARGET nkr_groth16_cuda)
    target_compile_definitions(nkr_groth16_common PUBLIC NKR_GROTH16_ACCELERATOR_CUDA=1)
elseif(NKR_ZKP_GROTH16_BACKEND STREQUAL "DCU")
    enable_language(HIP)
    add_subdirectory(backends/dcu)
    set(NKR_ZKP_GROTH16_BACKEND_TARGET nkr_groth16_dcu)
    target_compile_definitions(nkr_groth16_common PUBLIC NKR_GROTH16_ACCELERATOR_DCU=1)
elseif(NOT NKR_ZKP_GROTH16_BACKEND STREQUAL "CPU")
    message(FATAL_ERROR "Unsupported Groth16 backend: ${NKR_ZKP_GROTH16_BACKEND}")
endif()

foreach(app gen setup prove verify)
    add_executable(groth16-${app} "app/${app}.cpp")
    target_link_libraries(groth16-${app} PRIVATE
        nkr_groth16_common nkr_groth16_cpu ${NKR_ZKP_GROTH16_BACKEND_TARGET})
    target_compile_definitions(groth16-${app} PRIVATE
        APP_DATA_DIR="${NKR_ZKP_GROTH16_DATA_DIR}")
endforeach()

if(NKR_ZKP_GROTH16_BACKEND STREQUAL "CUDA")
    set_target_properties(groth16-setup groth16-prove PROPERTIES LINKER_LANGUAGE CUDA)
endif()
