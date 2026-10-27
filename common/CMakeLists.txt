add_library(nkr_common INTERFACE)
target_compile_features(nkr_common INTERFACE cxx_std_17)
target_include_directories(nkr_common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
