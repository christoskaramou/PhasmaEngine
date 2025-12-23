cmake_minimum_required(VERSION 3.10)
project(Scripts CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Scripts = ${CMAKE_CURRENT_SOURCE_DIR}")

# --- Compiler flags (same style as PhasmaEditor) ---
if(MSVC)
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP${N}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP${N}")
    endif()

    # /Oi = intrinsics, /permissive- like PhasmaEditor
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} /Oi /permissive-")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Oi /permissive-")
endif()

# --- Output directories (mirrors PhasmaEditor) ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo")

message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO}")

# --- Platform defines (kept simple) ---
set(PE_DEFS)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Detected operating system: Linux")
    list(APPEND PE_DEFS PE_LINUX _CONSOLE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Detected operating system: Windows")
    list(APPEND PE_DEFS
        PE_WIN32
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        _CONSOLE
    )
else()
    message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
endif()

# --- Sources ---
# All .cpp files under Assets/Scripts
file(GLOB_RECURSE SCRIPT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.pecpp"
)

# Tell CMake that .pecpp files should be treated as C++ sources
list(APPEND CMAKE_CXX_SOURCE_FILE_EXTENSIONS pecpp)

# --- Target ---
add_library(Scripts SHARED ${SCRIPT_SOURCES})
foreach(PROJ_SOURCE ${SCRIPT_SOURCES})
    set_source_files_properties(${PROJ_SOURCE} PROPERTIES LANGUAGE CXX)
endforeach()
set_target_properties(Scripts PROPERTIES LINKER_LANGUAGE CXX)

target_compile_definitions(Scripts PRIVATE ${PE_DEFS})

# If scripts need extra include dirs add them here
target_include_directories(Scripts PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
set(CMAKE_CXX_OUTPUT_EXTENSION .o)
set_source_files_properties(${SCRIPT_SOURCES} PROPERTIES LANGUAGE CXX)
if(NOT MSVC)
    set_source_files_properties(${SCRIPT_SOURCES} PROPERTIES COMPILE_FLAGS "-x c++")
endif()
