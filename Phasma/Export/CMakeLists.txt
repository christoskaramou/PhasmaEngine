cmake_minimum_required(VERSION 3.22)
project(PhasmaExport CXX)

add_executable(PhasmaExport "main.cpp")
target_precompile_headers(PhasmaExport PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_link_libraries(PhasmaExport PRIVATE PhasmaRuntime Lua)

if(COMMAND pe_depend_on_root_runtime_dependencies)
    pe_depend_on_root_runtime_dependencies(PhasmaExport)
endif()
