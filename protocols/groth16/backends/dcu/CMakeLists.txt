add_library(nkr_groth16_dcu STATIC
    src/api.hip
    src/groth16_backend.hip)
target_compile_features(nkr_groth16_dcu PUBLIC cxx_std_17 hip_std_17)
target_compile_options(nkr_groth16_dcu PRIVATE
    $<$<COMPILE_LANGUAGE:HIP>:-O2 -fgpu-rdc -gline-tables-only>)
target_include_directories(nkr_groth16_dcu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nkr_groth16_dcu PUBLIC nkr_groth16_common)
target_compile_definitions(nkr_groth16_dcu PUBLIC CURVE_ALT_BN128)
set_target_properties(nkr_groth16_dcu PROPERTIES
    HIP_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON)

if(NKR_ZKP_GROTH16_BUILD_LEGACY_TESTS)
    file(GLOB LEGACY_DCU_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.hip")
    foreach(test_source ${LEGACY_DCU_TESTS})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(dcu-${test_name} ${test_source})
        target_link_libraries(dcu-${test_name} PRIVATE nkr_groth16_dcu)
    endforeach()
endif()
