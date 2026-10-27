add_library(nkr_operators_hip INTERFACE)
target_compile_features(nkr_operators_hip INTERFACE cxx_std_17 hip_std_17)
target_include_directories(nkr_operators_hip INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(nkr_operators_hip INTERFACE CURVE_ALT_BN128)
