cmake_minimum_required(VERSION 3.21)

project(NKR_ZKP LANGUAGES CXX)

set(NKR_ZKP_ACCELERATOR "NONE" CACHE STRING
    "Optional accelerator to build alongside CPU: NONE, CUDA, or HIP")
set_property(CACHE NKR_ZKP_ACCELERATOR PROPERTY STRINGS NONE CUDA HIP)
string(TOUPPER "${NKR_ZKP_ACCELERATOR}" NKR_ZKP_ACCELERATOR)

set(NKR_ZKP_LIGHTZK_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lightzk/depends"
    CACHE PATH "Path to the vendored lightZK dependencies")

find_package(OpenMP REQUIRED)

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

add_subdirectory(common)
add_subdirectory(operators)
add_subdirectory(protocols/groth16)
add_subdirectory(protocols/plonk)
