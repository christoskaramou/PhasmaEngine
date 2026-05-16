cmake_minimum_required(VERSION 3.10)
project(PhasmaRuntime CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaRuntime = ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE PHASMARUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.h"
)

add_library(PhasmaRuntime STATIC ${PHASMARUNTIME_SOURCES})
set_target_properties(PhasmaRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_precompile_headers(PhasmaRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/pch/PhasmaPch.h")

target_include_directories(PhasmaRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Code")
target_include_directories(PhasmaRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/rapidjson/include")
target_link_libraries(PhasmaRuntime PUBLIC PhasmaCore)
