cmake_minimum_required(VERSION 3.24)
project(PhasmaPlayer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaPlayer = ${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(PhasmaPlayer "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

target_precompile_headers(PhasmaPlayer PRIVATE "${CMAKE_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")
target_link_directories(PhasmaPlayer PRIVATE ${PE_CORE_LINK_DIRS})
target_link_libraries(PhasmaPlayer PRIVATE PhasmaCore "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>" SDL2 SDL2main)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaPlayer PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(COMMAND pe_depend_on_root_runtime_dependencies)
    pe_depend_on_root_runtime_dependencies(PhasmaPlayer)
endif()
