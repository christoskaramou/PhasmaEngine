cmake_minimum_required(VERSION 3.15)
project(PhasmaAgent CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "PhasmaAgent = ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE PAGENT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(PhasmaAgent STATIC ${PAGENT_SOURCES})

# Public: only the clean Agent.h (no third-party types leak out)
# Private: internal src + vendored single headers
target_include_directories(PhasmaAgent
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

# --- OpenSSL (optional — provides built-in HTTPS; not needed if custom_http_handler is used) ---
option(PAGENT_USE_OPENSSL "Link OpenSSL for built-in HTTPS support" ON)

if(PAGENT_USE_OPENSSL)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND OR OPENSSL_FOUND)
        target_link_libraries(PhasmaAgent PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(PhasmaAgent PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
        message(STATUS "[PhasmaAgent] OpenSSL ${OPENSSL_VERSION} found — built-in HTTPS enabled")
    else()
        message(WARNING "[PhasmaAgent] OpenSSL not found — built-in HTTPS disabled.\n"
            "  Install OpenSSL (vcpkg: vcpkg install openssl:x64-windows) or set OPENSSL_ROOT_DIR,\n"
            "  or provide AgentConfig::custom_http_handler at runtime.")
    endif()
else()
    message(STATUS "[PhasmaAgent] PAGENT_USE_OPENSSL=OFF — provide AgentConfig::custom_http_handler at runtime")
endif()

# --- Threading ---
find_package(Threads REQUIRED)
target_link_libraries(PhasmaAgent PRIVATE Threads::Threads)

# --- Platform-specific ---
if(WIN32)
    target_link_libraries(PhasmaAgent PRIVATE ws2_32 crypt32)
    target_compile_definitions(PhasmaAgent PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
