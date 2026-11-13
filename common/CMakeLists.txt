add_library(nkr_common STATIC src/profile.cpp)
target_compile_features(nkr_common PUBLIC cxx_std_17)
target_include_directories(nkr_common
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${NKR_ZKP_LIGHTZK_DEPS_DIR}/libsnark/depends/libff")
target_link_libraries(nkr_common PUBLIC snark)
