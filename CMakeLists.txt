cmake_minimum_required(VERSION 3.21)

project(NKR_ZKP LANGUAGES CXX)

set(NKR_ZKP_GROTH16_ACCELERATOR "NONE" CACHE STRING
    "Optional Groth16 accelerator to build alongside CPU: NONE, CUDA, or DCU")
set_property(CACHE NKR_ZKP_GROTH16_ACCELERATOR PROPERTY STRINGS NONE CUDA DCU)

string(TOUPPER "${NKR_ZKP_GROTH16_ACCELERATOR}" NKR_ZKP_GROTH16_ACCELERATOR)

add_subdirectory(protocols/groth16)
