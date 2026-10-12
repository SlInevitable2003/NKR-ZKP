cmake_minimum_required(VERSION 3.21)

project(NKR_ZKP LANGUAGES CXX)

set(NKR_ZKP_GROTH16_BACKEND "NONE" CACHE STRING
    "Groth16 backend to build: NONE, CUDA, or DCU")
set_property(CACHE NKR_ZKP_GROTH16_BACKEND PROPERTY STRINGS NONE CUDA DCU)

string(TOUPPER "${NKR_ZKP_GROTH16_BACKEND}" NKR_ZKP_GROTH16_BACKEND)

if(NKR_ZKP_GROTH16_BACKEND STREQUAL "CUDA")
    add_subdirectory(protocols/groth16/backends/cuda)
elseif(NKR_ZKP_GROTH16_BACKEND STREQUAL "DCU")
    add_subdirectory(protocols/groth16/backends/dcu)
elseif(NOT NKR_ZKP_GROTH16_BACKEND STREQUAL "NONE")
    message(FATAL_ERROR
        "NKR_ZKP_GROTH16_BACKEND must be one of NONE, CUDA, or DCU")
endif()

