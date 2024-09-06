cmake_minimum_required(VERSION 3.5)
set(PROJECT_NAME Phasma)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP${N}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP${N}")   
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Oi")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Oi") 
endif()
message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release")

message("CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message("CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}")
message("CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}")

# Phasma directories
set(PHASMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Phasma")
set(INCLUDE_DIR "${PHASMA_DIR}/Include")
set(LIBS_DIR "${PHASMA_DIR}/Libs")
set(DLLS_DIR "${PHASMA_DIR}/DLLs")
set(ASSETS_DIR "${PHASMA_DIR}/Assets")

# Include directories
include_directories("${PHASMA_DIR}")
include_directories("${PHASMA_DIR}/Code")
include_directories("${INCLUDE_DIR}")
include_directories("${INCLUDE_DIR}/SDL")
include_directories("${INCLUDE_DIR}/imgui")
include_directories("${INCLUDE_DIR}/FSR2")

# Link directories
link_directories("${LIBS_DIR}/dx")
link_directories("${LIBS_DIR}/fsr/Debug")
link_directories("${LIBS_DIR}/fsr/Release")
link_directories("${LIBS_DIR}/sdl2")
link_directories("${LIBS_DIR}/spirv")
link_directories("${LIBS_DIR}/spirv/Debug")
link_directories("${LIBS_DIR}/spirv/Release")
link_directories("${LIBS_DIR}/vulkan")

# Source files
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${PHASMA_DIR}/*.cpp")
add_executable(${PROJECT_NAME} ${SRC_FILES})

# Precompiled headers
target_precompile_headers(${PROJECT_NAME} PRIVATE "${INCLUDE_DIR}/PhasmaPch.h")

set (PE_VULKAN 0)
set (PE_DX12 1)

# Compile Definitions
set(ADDITIONAL_COMPILE_DEFINITIONS
    $<$<CONFIG:Debug>:
        _DEBUG
    >
    $<$<CONFIG:Release>:
        NDEBUG
    >
    WIN32
    _CONSOLE
    UNICODE
    _UNICODE
    PE_RENDERING_API=${PE_VULKAN}
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_compile_definitions(${PROJECT_NAME} PRIVATE "${ADDITIONAL_COMPILE_DEFINITIONS}")

# Libraries
set(LIB_FILES
    $<$<CONFIG:Debug>:
        spirv-cross-cored
        ffx_fsr2_api_x64d
        ffx_fsr2_api_vk_x64d
    >
    $<$<CONFIG:Release>:
        spirv-cross-core
        ffx_fsr2_api_x64
        ffx_fsr2_api_vk_x64
    >
    shaderc_shared
    dxcompiler
    SDL2main
    SDL2
    vulkan-1
)
target_link_libraries(${PROJECT_NAME} PRIVATE "${LIB_FILES}")

# Copy assets and DLLs to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLLS_DIR}/SDL2.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLLS_DIR}/shaderc_shared.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLLS_DIR}/dxcompiler.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_DIR}" "$<CONFIG>/Assets"
)
