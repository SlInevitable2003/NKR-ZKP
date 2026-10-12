cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
project(lightZK LANGUAGES CXX HIP)

# Set C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_HIP_STANDARD 17)
set(NKR_ZKP_GROTH16_DEPS_DIR "" CACHE PATH
    "Path to the lightZK depends directory containing libsnark")
if(NOT IS_DIRECTORY "${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark")
    message(FATAL_ERROR
        "Set NKR_ZKP_GROTH16_DEPS_DIR to a lightZK depends directory")
endif()

# Set CUDA Standard
#set(CMAKE_CUDA_STANDARD 17)
#set(CMAKE_CUDA_STANDARD_REQUIRED ON)
# set(CMAKE_CUDA_ARCHITECTURES 120)

# Set CUDA Flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -fopenmp -Wno-return-type")
set(CMAKE_HIP_FLAGS "${CMAKE_HIP_FLAGS} -O1 -fopenmp -Wno-return-type -fgpu-rdc -gline-tables-only")
#set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -O1 --extended-lambda -lineinfo")
#set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -O2 --extended-lambda -lineinfo -Xcompiler -fopenmp")

#add_definitions(-D__HIP_PLATFORM_AMD__)
#add_definitions(-DTHRUST_DEVICE_SYSTEM=THRUST_DEVICE_SYSTEM_HIP)

# Set Searching Path for Header Files
include_directories(${PROJECT_SOURCE_DIR}/include)
get_filename_component(NKR_ZKP_GROTH16_ROOT "${PROJECT_SOURCE_DIR}/../.." ABSOLUTE)
set(NKR_ZKP_GROTH16_COMMON_INCLUDE "${NKR_ZKP_GROTH16_ROOT}/common/include")
include_directories(${NKR_ZKP_GROTH16_COMMON_INCLUDE})

# Collecting source codes
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.c"
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.cu"
	"${PROJECT_SOURCE_DIR}/src/*.hip"
)

# Add Core Library
add_library(libLightZK STATIC ${SRC_FILES})

set_target_properties(libLightZK PROPERTIES
	HIP_SEPARABLE_COMPILATION ON
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

# 编译测试程序                                                                                                                                                                                  
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/*.hip")
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    set_target_properties(${test_name} PROPERTIES HIP_SEPARABLE_COMPILATION ON)
    target_link_libraries(${test_name} PRIVATE 
		libLightZK
		snark
	)
    target_include_directories(${test_name} PRIVATE 
		${PROJECT_SOURCE_DIR}/include
		${NKR_ZKP_GROTH16_DEPS_DIR}/libsnark
		#		${PROJECT_SOURCE_DIR}/depends/cgbn
	)
endforeach()
