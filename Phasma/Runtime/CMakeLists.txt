cmake_minimum_required(VERSION 3.22)
project(PhasmaRuntime CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaRuntime = ${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT DEFINED PE_SHARED_THIRD_PARTY_DIR)
    set(PE_SHARED_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
endif()

# Assimp lives only in PhasmaCook (the desktop mesh-cook tool). The runtime library never compiles
# ModelAssetAssimp, never defines PE_ENABLE_ASSIMP, and never links Assimp on any platform; source
# models reach the runtime exclusively as cooked ".pemesh".
file(GLOB_RECURSE PHASMARUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.h"
)
list(FILTER PHASMARUNTIME_SOURCES EXCLUDE REGEX "/UI/Backends/")

# The opt-in player MCP server (Code/Agent/, Code/Script/Bindings/Agent/) depends on PhasmaMCP. When
# MCP is not built (e.g. minimal / shipped player), drop those sources so the runtime still compiles and
# the server cannot exist in the binary at all.
if(NOT (PE_BUILD_MCP AND TARGET PhasmaMCP))
    list(FILTER PHASMARUNTIME_SOURCES EXCLUDE REGEX "/Code/Agent/")
    list(FILTER PHASMARUNTIME_SOURCES EXCLUDE REGEX "/Bindings/Agent/")
endif()

add_library(PhasmaRuntime STATIC ${PHASMARUNTIME_SOURCES})
set_target_properties(PhasmaRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_precompile_headers(PhasmaRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Core/pch/PhasmaPch.h")

target_include_directories(PhasmaRuntime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Code"
    "${PE_SHARED_THIRD_PARTY_DIR}/lua"
    "${PE_SHARED_THIRD_PARTY_DIR}/sol2"
)
target_include_directories(PhasmaRuntime PRIVATE
    "${PE_SHARED_THIRD_PARTY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party/meshoptimizer"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party/rapidjson/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party/spirv_cross"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party/vma"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Core/third_party/vulkan"
)

target_link_libraries(PhasmaRuntime PUBLIC PhasmaCore)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaRuntime PUBLIC ${PE_CORE_LINK_DIRS})
endif()
if(DEFINED PE_SPIRV_CROSS_CORE_LIB)
    target_link_libraries(PhasmaRuntime PUBLIC ${PE_SPIRV_CROSS_CORE_LIB})
endif()
target_link_libraries(PhasmaRuntime PUBLIC vulkan)
if(WIN32)
    target_link_libraries(PhasmaRuntime PUBLIC d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)
endif()
if(PE_PHYSICS AND TARGET Jolt)
    target_compile_definitions(PhasmaRuntime PRIVATE PE_PHYSICS)
    target_link_libraries(PhasmaRuntime PUBLIC Jolt)
endif()
if(PE_PHYSICS2D AND TARGET box2d)
    target_compile_definitions(PhasmaRuntime PRIVATE PE_PHYSICS2D)
    target_link_libraries(PhasmaRuntime PUBLIC box2d)
endif()
if(PE_AUDIO)
    target_include_directories(PhasmaRuntime PRIVATE "${PE_SHARED_THIRD_PARTY_DIR}/miniaudio")
    target_compile_definitions(PhasmaRuntime PRIVATE PE_AUDIO)
endif()
if(TARGET meshoptimizer)
    target_link_libraries(PhasmaRuntime PRIVATE meshoptimizer)
endif()
if(TARGET Lua)
    target_link_libraries(PhasmaRuntime PUBLIC Lua)
endif()

# Opt-in player MCP server. PUBLIC so PhasmaMCP's include dirs (nlohmann, the pmcp headers) reach the
# runtime's Agent sources; PE_PLAYER_MCP gates the player-host wiring in always-compiled files.
if(PE_BUILD_MCP AND TARGET PhasmaMCP)
    target_link_libraries(PhasmaRuntime PUBLIC PhasmaMCP)
    target_compile_definitions(PhasmaRuntime PRIVATE PE_PLAYER_MCP)
endif()

if(MSVC)
    target_compile_options(PhasmaRuntime PRIVATE $<$<CONFIG:Debug>:/O1>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(PhasmaRuntime PRIVATE $<$<CONFIG:Debug>:-O1>)
endif()
