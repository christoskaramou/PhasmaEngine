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

option(PE_WEBGPU_SHARED "Build PhasmaWebGPU as a shared library" OFF)

if(PE_WEBGPU_SHARED)
    add_library(PhasmaWebGPU SHARED ${PWGPU_SOURCES})
    target_compile_definitions(PhasmaWebGPU
        PRIVATE PWGPU_BUILDING PWGPU_SHARED_LIBRARY WGPU_SHARED_LIBRARY WGPU_IMPLEMENTATION
        PUBLIC  PWGPU_SHARED_LIBRARY)
else()
    add_library(PhasmaWebGPU STATIC ${PWGPU_SOURCES})
endif()

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

# --- Smoke test executable ---
option(PE_WEBGPU_TESTS "Build PhasmaWebGPU smoke tests" ON)
if(PE_WEBGPU_TESTS)
    add_executable(WebGPUSmokeTest "${CMAKE_CURRENT_SOURCE_DIR}/Tests/SmokeTest.cpp")
    target_link_libraries(WebGPUSmokeTest PRIVATE PhasmaWebGPU)
    target_precompile_headers(WebGPUSmokeTest REUSE_FROM PhasmaWebGPU)
    target_include_directories(WebGPUSmokeTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Code"
    )
    set_target_properties(WebGPUSmokeTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(WebGPURenderTest "${CMAKE_CURRENT_SOURCE_DIR}/Tests/RenderTest.cpp")
    target_link_libraries(WebGPURenderTest PRIVATE PhasmaWebGPU)
    target_precompile_headers(WebGPURenderTest REUSE_FROM PhasmaWebGPU)
    target_include_directories(WebGPURenderTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Code"
    )
endif()
