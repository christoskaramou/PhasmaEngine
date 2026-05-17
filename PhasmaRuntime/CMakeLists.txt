cmake_minimum_required(VERSION 3.10)
project(PhasmaRuntime CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaRuntime = ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE PHASMARUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.h"
)
file(GLOB_RECURSE PHASMARUNTIME_CACAO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/CACAO/*.cpp"
)

add_library(PhasmaRuntime STATIC ${PHASMARUNTIME_SOURCES} ${PHASMARUNTIME_CACAO_SOURCES})
set_target_properties(PhasmaRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_precompile_headers(PhasmaRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/pch/PhasmaPch.h")

target_include_directories(PhasmaRuntime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Code"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/assimp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/sol2"
)
target_include_directories(PhasmaRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/meshoptimizer"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/rapidjson/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/spirv_cross"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/vma"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/vulkan"
)

if(WIN32)
    target_compile_definitions(PhasmaRuntime PRIVATE FFX_CACAO_ENABLE_D3D12)
    target_include_directories(PhasmaRuntime PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/CacaoCompat"
        "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/CacaoCompat/DirectXHeaders/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/CacaoCompat/DirectXHeaders/include/directx"
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set_source_files_properties(${PHASMARUNTIME_CACAO_SOURCES} PROPERTIES
            COMPILE_FLAGS "/permissive /Zc:gotoScope-"
            SKIP_PRECOMPILE_HEADERS ON)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set_source_files_properties(${PHASMARUNTIME_CACAO_SOURCES} PROPERTIES
            COMPILE_FLAGS "-Wno-address-of-temporary")
    endif()
endif()
target_link_libraries(PhasmaRuntime PUBLIC PhasmaCore)
if(DEFINED PE_CORE_LINK_DIRS)
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
if(PE_AUDIO)
    target_include_directories(PhasmaRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party/miniaudio")
    target_compile_definitions(PhasmaRuntime PRIVATE PE_AUDIO)
endif()
if(TARGET assimp)
    target_link_libraries(PhasmaRuntime PRIVATE assimp)
endif()
if(TARGET meshoptimizer)
    target_link_libraries(PhasmaRuntime PRIVATE meshoptimizer)
endif()
if(TARGET Lua)
    target_link_libraries(PhasmaRuntime PUBLIC Lua)
endif()
