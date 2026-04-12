cmake_minimum_required(VERSION 3.15)
project(PhasmaWebGPU CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "PhasmaWebGPU = ${CMAKE_CURRENT_SOURCE_DIR}")

# --- webgpu-native headers ---
include(FetchContent)
FetchContent_Declare(webgpu_headers
    GIT_REPOSITORY https://github.com/webgpu-native/webgpu-headers.git
    GIT_TAG        main
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(webgpu_headers)

# The webgpu-native headers repo ships webgpu.h at the repo root (no webgpu/ subdir).
# Create a webgpu/ wrapper directory so code can include <webgpu/webgpu.h> as per spec.
set(WEBGPU_INCLUDE_WRAP "${CMAKE_BINARY_DIR}/webgpu_include")
file(MAKE_DIRECTORY "${WEBGPU_INCLUDE_WRAP}/webgpu")
configure_file(
    "${webgpu_headers_SOURCE_DIR}/webgpu.h"
    "${WEBGPU_INCLUDE_WRAP}/webgpu/webgpu.h"
    COPYONLY
)

file(GLOB_RECURSE PWGPU_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp")

add_library(PhasmaWebGPU STATIC ${PWGPU_SOURCES})

set_target_properties(PhasmaWebGPU PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_precompile_headers(PhasmaWebGPU PRIVATE
    "${CMAKE_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h"
)

target_include_directories(PhasmaWebGPU
    PUBLIC  "${WEBGPU_INCLUDE_WRAP}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Code"
)

target_link_libraries(PhasmaWebGPU PUBLIC PhasmaCore)

if(MSVC)
    target_compile_options(PhasmaWebGPU PRIVATE /W3)
else()
    target_compile_options(PhasmaWebGPU PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
