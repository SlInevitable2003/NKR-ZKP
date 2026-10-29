if(NOT IS_DIRECTORY "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark")
    message(FATAL_ERROR "lightZK dependencies not found at ${NKR_ZKP_LIGHTZK_DEPS_DIR}")
endif()

set(NKR_ZKP_GROTH16_BIN_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH
    "Directory for Groth16 executables")
set(NKR_ZKP_GROTH16_DATA_DIR "${CMAKE_BINARY_DIR}/data" CACHE PATH
    "Directory for Groth16 test artifacts")
option(NKR_ZKP_GROTH16_BUILD_LEGACY_TESTS "Build imported backend microbenchmarks" OFF)

add_library(nkr_groth16_common STATIC
    common/src/backend_registry.cpp
    common/src/io.cpp
    common/src/protocol.cpp)
target_compile_features(nkr_groth16_common PUBLIC cxx_std_17)
target_include_directories(nkr_groth16_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include"
    "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark")
target_compile_definitions(nkr_groth16_common PUBLIC CURVE_ALT_BN128)
target_link_libraries(nkr_groth16_common PUBLIC
    nkr_common nkr_operators_common snark OpenMP::OpenMP_CXX)

add_library(nkr_groth16_cpu STATIC backends/cpu/src/backend.cpp)
target_link_libraries(nkr_groth16_cpu PUBLIC nkr_groth16_common)

set(NKR_ZKP_GROTH16_ACCELERATOR_TARGET "")
if(NKR_ZKP_ACCELERATOR STREQUAL "CUDA")
    add_subdirectory(backends/cuda)
    set(NKR_ZKP_GROTH16_ACCELERATOR_TARGET nkr_groth16_cuda)
    target_compile_definitions(nkr_groth16_common PRIVATE NKR_GROTH16_ACCELERATOR_CUDA=1)
elseif(NKR_ZKP_ACCELERATOR STREQUAL "HIP")
    add_subdirectory(backends/hip)
    set(NKR_ZKP_GROTH16_ACCELERATOR_TARGET nkr_groth16_hip)
    target_compile_definitions(nkr_groth16_common PRIVATE NKR_GROTH16_ACCELERATOR_HIP=1)
elseif(NOT NKR_ZKP_ACCELERATOR STREQUAL "NONE")
    message(FATAL_ERROR
        "Unsupported accelerator: ${NKR_ZKP_ACCELERATOR}")
endif()

foreach(app gen verify)
    add_executable(groth16-${app} "app/${app}.cpp")
    set_target_properties(groth16-${app} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NKR_ZKP_GROTH16_BIN_DIR}")
    target_link_libraries(groth16-${app} PRIVATE nkr_groth16_common)
    target_compile_definitions(groth16-${app} PRIVATE
        APP_DATA_DIR="${NKR_ZKP_GROTH16_DATA_DIR}")
endforeach()

foreach(app setup prove)
    add_executable(groth16-${app} "app/${app}.cpp")
    set_target_properties(groth16-${app} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NKR_ZKP_GROTH16_BIN_DIR}")
    target_link_libraries(groth16-${app} PRIVATE
        nkr_groth16_common nkr_groth16_cpu ${NKR_ZKP_GROTH16_ACCELERATOR_TARGET})
    target_compile_definitions(groth16-${app} PRIVATE
        APP_DATA_DIR="${NKR_ZKP_GROTH16_DATA_DIR}")
endforeach()

if(NKR_ZKP_ACCELERATOR STREQUAL "CUDA")
    set_target_properties(groth16-setup groth16-prove PROPERTIES LINKER_LANGUAGE CUDA)
endif()
