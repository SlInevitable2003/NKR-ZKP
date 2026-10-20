add_library(nkr_groth16_cuda STATIC
    src/api.cu
    src/utils.cu
    src/groth16_backend.cu)
target_compile_features(nkr_groth16_cuda PUBLIC cxx_std_17 cuda_std_17)
target_compile_options(nkr_groth16_cuda PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-O2 --extended-lambda -lineinfo>)
target_include_directories(nkr_groth16_cuda PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NKR_ZKP_GROTH16_DEPS_DIR}/cgbn")
target_link_libraries(nkr_groth16_cuda PUBLIC nkr_groth16_common)
target_compile_definitions(nkr_groth16_cuda PUBLIC CURVE_ALT_BN128)
set_target_properties(nkr_groth16_cuda PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

if(NKR_ZKP_GROTH16_BUILD_LEGACY_TESTS)
    file(GLOB LEGACY_CUDA_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cu")
    foreach(test_source ${LEGACY_CUDA_TESTS})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cuda-${test_name} ${test_source})
        target_link_libraries(cuda-${test_name} PRIVATE nkr_groth16_cuda)
    endforeach()
endif()
