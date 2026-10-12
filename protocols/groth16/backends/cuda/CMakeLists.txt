cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

project(lightZK LANGUAGES CXX CUDA)
# Set C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set CUDA Standard
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(NKR_ZKP_GROTH16_DEPS_DIR "" CACHE PATH
    "Path to the lightZK depends directory containing libsnark and cgbn")
if(NOT IS_DIRECTORY "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark")
    message(FATAL_ERROR
        "Set NKR_ZKP_GROTH16_DEPS_DIR to a lightZK depends directory")
endif()

# Set CUDA Flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -O2 --extended-lambda -lineinfo")

# Set Searching Path for Header Files
get_filename_component(NKR_ZKP_GROTH16_ROOT "${PROJECT_SOURCE_DIR}/../.." ABSOLUTE)
set(NKR_ZKP_GROTH16_COMMON_INCLUDE "${NKR_ZKP_GROTH16_ROOT}/common/include")
include_directories(${PROJECT_SOURCE_DIR}/include ${NKR_ZKP_GROTH16_COMMON_INCLUDE})

# Collecting source codes
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.c"
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.cu"
)

# Add Core Library
add_library(libLightZK STATIC ${SRC_FILES})

set_target_properties(libLightZK PROPERTIES
	CUDA_SEPARABLE_COMPILATION ON
	POSITION_INDEPENDENT_CODE ON
)

target_include_directories(libLightZK PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${NKR_ZKP_GROTH16_COMMON_INCLUDE})

set(CURVE "ALT_BN128" CACHE STRING "Default curve: one of ALT_BN128, BN128, EDWARDS, MNT4, MNT6")
add_definitions(-DCURVE_${CURVE})
add_subdirectory(
    "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark"
    "${CMAKE_CURRENT_BINARY_DIR}/depends/libsnark")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -fopenmp")

# 编译测试程序                                                                                                                                                                                  
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/*.cu")
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE 
		libLightZK
		snark
	)
    target_include_directories(${test_name} PRIVATE 
		${PROJECT_SOURCE_DIR}/include
		${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark
		${NKR_ZKP_GROTH16_DEPS_DIR}/cgbn
	)
endforeach()

# 数据文件目录 (app 程序读写 R1CS 文件)
set(APP_DATA_DIR "${PROJECT_SOURCE_DIR}/app/data" CACHE PATH "Directory for app data files")

# 编译 app 程序
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/app/*.cu")
foreach(app_src ${APP_SOURCES})
    get_filename_component(app_name ${app_src} NAME_WE)
    add_executable(${app_name} ${app_src})
    target_link_libraries(${app_name} PRIVATE 
		libLightZK
		snark
	)
    target_include_directories(${app_name} PRIVATE 
		${PROJECT_SOURCE_DIR}/include
		${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark
		${NKR_ZKP_GROTH16_DEPS_DIR}/cgbn
	)
    target_compile_definitions(${app_name} PRIVATE APP_DATA_DIR="${APP_DATA_DIR}")
endforeach()
