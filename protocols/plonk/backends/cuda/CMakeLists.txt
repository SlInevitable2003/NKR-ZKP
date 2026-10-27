add_library(nkr_plonk_cuda STATIC src/backend.cu)
target_compile_features(nkr_plonk_cuda PUBLIC cxx_std_17 cuda_std_17)
target_compile_options(nkr_plonk_cuda PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-O2 --extended-lambda -lineinfo>)
target_link_libraries(nkr_plonk_cuda PUBLIC
    nkr_plonk_common nkr_operators_cuda)
set_target_properties(nkr_plonk_cuda PROPERTIES POSITION_INDEPENDENT_CODE ON)
