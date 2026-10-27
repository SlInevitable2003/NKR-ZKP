set(NKR_ZKP_PLONK_BIN_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH
    "Directory for PLONK executables")
set(NKR_ZKP_PLONK_DATA_DIR "${CMAKE_BINARY_DIR}/plonk-data" CACHE PATH
    "Directory for PLONK artifacts")

add_library(nkr_plonk_common STATIC
    common/src/arithmetization.cpp
    common/src/backend_registry.cpp
    common/src/io.cpp
    common/src/profile.cpp
    common/src/protocol.cpp
    common/src/transcript.cpp)
target_compile_features(nkr_plonk_common PUBLIC cxx_std_17)
target_include_directories(nkr_plonk_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include"
    "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark")
target_compile_definitions(nkr_plonk_common PUBLIC CURVE_ALT_BN128)
target_link_libraries(nkr_plonk_common PUBLIC nkr_common snark OpenMP::OpenMP_CXX)

add_library(nkr_plonk_cpu STATIC backends/cpu/src/backend.cpp)
target_link_libraries(nkr_plonk_cpu PUBLIC nkr_plonk_common)

add_executable(plonk-arithmetization-test EXCLUDE_FROM_ALL
    tests/arithmetization_test.cpp)
target_link_libraries(plonk-arithmetization-test PRIVATE nkr_plonk_common)

add_executable(plonk-cpu-setup-test EXCLUDE_FROM_ALL
    tests/cpu_setup_test.cpp)
target_link_libraries(plonk-cpu-setup-test PRIVATE nkr_plonk_cpu)

add_executable(plonk-cpu-prove-verify-test EXCLUDE_FROM_ALL
    tests/cpu_prove_verify_test.cpp)
target_link_libraries(plonk-cpu-prove-verify-test PRIVATE nkr_plonk_cpu)

set(NKR_ZKP_PLONK_ACCELERATOR_TARGET "")
if(NKR_ZKP_ACCELERATOR STREQUAL "CUDA")
    add_subdirectory(backends/cuda)
    set(NKR_ZKP_PLONK_ACCELERATOR_TARGET nkr_plonk_cuda)
    target_compile_definitions(nkr_plonk_common PRIVATE NKR_PLONK_ACCELERATOR_CUDA=1)
elseif(NKR_ZKP_ACCELERATOR STREQUAL "HIP")
    add_subdirectory(backends/hip)
    set(NKR_ZKP_PLONK_ACCELERATOR_TARGET nkr_plonk_hip)
    target_compile_definitions(nkr_plonk_common PRIVATE NKR_PLONK_ACCELERATOR_HIP=1)
endif()

foreach(app gen setup prove verify)
    add_executable(plonk-${app} "app/${app}.cpp")
    set_target_properties(plonk-${app} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NKR_ZKP_PLONK_BIN_DIR}")
    target_link_libraries(plonk-${app} PRIVATE
        nkr_plonk_common nkr_plonk_cpu ${NKR_ZKP_PLONK_ACCELERATOR_TARGET})
    target_compile_definitions(plonk-${app} PRIVATE
        APP_DATA_DIR="${NKR_ZKP_PLONK_DATA_DIR}")
endforeach()

if(NKR_ZKP_ACCELERATOR STREQUAL "CUDA")
    set_target_properties(plonk-gen plonk-setup plonk-prove plonk-verify
        PROPERTIES LINKER_LANGUAGE CUDA)
endif()
