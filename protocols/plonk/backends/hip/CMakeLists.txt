add_library(nkr_plonk_hip STATIC src/backend.hip)
target_compile_features(nkr_plonk_hip PUBLIC cxx_std_17 hip_std_17)
target_compile_options(nkr_plonk_hip PRIVATE
    $<$<COMPILE_LANGUAGE:HIP>:-O2 -fgpu-rdc -gline-tables-only>)
# Static HIP objects are compiled as relocatable device code. DCC requires the
# final consumer to enter HIP device-link mode explicitly.
target_link_options(nkr_plonk_hip INTERFACE
    $<$<LINK_LANGUAGE:HIP>:-fgpu-rdc>
    $<$<LINK_LANGUAGE:HIP>:--hip-link>)
target_link_libraries(nkr_plonk_hip PUBLIC
    nkr_plonk_common nkr_operators_hip)
set_target_properties(nkr_plonk_hip PROPERTIES
    HIP_SEPARABLE_COMPILATION ON POSITION_INDEPENDENT_CODE ON)

add_executable(plonk-hip-operators-test EXCLUDE_FROM_ALL
    test/operators_test.hip)
target_compile_features(plonk-hip-operators-test PRIVATE cxx_std_17 hip_std_17)
target_compile_options(plonk-hip-operators-test PRIVATE
    $<$<COMPILE_LANGUAGE:HIP>:-O2 -fgpu-rdc -gline-tables-only>)
target_link_libraries(plonk-hip-operators-test PRIVATE nkr_plonk_hip)
set_target_properties(plonk-hip-operators-test PROPERTIES
    HIP_SEPARABLE_COMPILATION ON)
