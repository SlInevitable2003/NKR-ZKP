add_library(nkr_operators_cuda STATIC src/runtime.cu)
target_compile_features(nkr_operators_cuda PUBLIC cxx_std_17 cuda_std_17)
target_compile_options(nkr_operators_cuda PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-O2 --extended-lambda -lineinfo>)
target_include_directories(nkr_operators_cuda PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NKR_ZKP_LIGHTZK_DEPS_DIR}/cgbn")
target_compile_definitions(nkr_operators_cuda PUBLIC CURVE_ALT_BN128)
set_target_properties(nkr_operators_cuda PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
