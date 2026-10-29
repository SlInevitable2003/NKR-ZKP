add_library(nkr_operators_common INTERFACE)
target_compile_features(nkr_operators_common INTERFACE cxx_std_17)
target_include_directories(nkr_operators_common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nkr_operators_common INTERFACE snark OpenMP::OpenMP_CXX)
