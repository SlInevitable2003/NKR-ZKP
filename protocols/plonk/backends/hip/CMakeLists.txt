add_library(nkr_plonk_hip STATIC src/backend.hip)
target_compile_features(nkr_plonk_hip PUBLIC cxx_std_17 hip_std_17)
target_compile_options(nkr_plonk_hip PRIVATE
    $<$<COMPILE_LANGUAGE:HIP>:-O2 -fgpu-rdc -gline-tables-only>)
target_link_libraries(nkr_plonk_hip PUBLIC
    nkr_plonk_common nkr_operators_hip)
set_target_properties(nkr_plonk_hip PROPERTIES
    HIP_SEPARABLE_COMPILATION ON POSITION_INDEPENDENT_CODE ON)
