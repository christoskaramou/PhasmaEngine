cmake_minimum_required(VERSION 3.15)
project(PhasmaAgent CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "PhasmaAgent = ${CMAKE_CURRENT_SOURCE_DIR}")

# --- tree-sitter (AST-aware code chunking) ---
include(FetchContent)

set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(TREE_SITTER_FEATURE_WASM OFF CACHE BOOL "" FORCE)

FetchContent_Declare(tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG        v0.24.7
    SOURCE_SUBDIR  lib
)
FetchContent_MakeAvailable(tree_sitter)

# tree-sitter-cpp grammar (download only - skip its CMake which needs tree-sitter CLI)
FetchContent_Declare(tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.23.4
    SOURCE_SUBDIR  _skip_cmake
)
FetchContent_MakeAvailable(tree_sitter_cpp)

add_library(tree_sitter_cpp_grammar STATIC
    "${tree_sitter_cpp_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c"
)
target_include_directories(tree_sitter_cpp_grammar PRIVATE
    "${tree_sitter_cpp_SOURCE_DIR}/src"
    "${tree_sitter_SOURCE_DIR}/lib/include"
)
target_link_libraries(tree_sitter_cpp_grammar PRIVATE tree-sitter)

set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)

file(GLOB_RECURSE PAGENT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(PhasmaAgent STATIC ${PAGENT_SOURCES})

# Public: only the clean Agent.h (no third-party types leak out)
# Private: internal src + vendored single headers
target_include_directories(PhasmaAgent
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
             "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Link tree-sitter (include dir is PRIVATE in their CMake, so add it explicitly)
target_include_directories(PhasmaAgent PRIVATE "${tree_sitter_SOURCE_DIR}/lib/include")
target_link_libraries(PhasmaAgent PRIVATE tree-sitter tree_sitter_cpp_grammar)

# --- OpenSSL (optional - provides built-in HTTPS; not needed if custom_http_handler is used) ---
option(PAGENT_USE_OPENSSL "Link OpenSSL for built-in HTTPS support" ON)

if(PAGENT_USE_OPENSSL)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND OR OPENSSL_FOUND)
        target_link_libraries(PhasmaAgent PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(PhasmaAgent PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
        message(STATUS "[PhasmaAgent] OpenSSL ${OPENSSL_VERSION} found - built-in HTTPS enabled")
        set(OPENSSL_FOUND TRUE PARENT_SCOPE)
    else()
        message(WARNING "[PhasmaAgent] OpenSSL not found - built-in HTTPS disabled.\n"
            "  Install OpenSSL (vcpkg: vcpkg install openssl:x64-windows) or set OPENSSL_ROOT_DIR,\n"
            "  or provide AgentConfig::custom_http_handler at runtime.")
    endif()
else()
    message(STATUS "[PhasmaAgent] PAGENT_USE_OPENSSL=OFF - provide AgentConfig::custom_http_handler at runtime")
endif()

# --- Threading ---
find_package(Threads REQUIRED)
target_link_libraries(PhasmaAgent PRIVATE Threads::Threads)

# --- Platform-specific ---
if(WIN32)
    target_link_libraries(PhasmaAgent PRIVATE ws2_32 crypt32)
    target_compile_definitions(PhasmaAgent PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

