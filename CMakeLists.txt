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

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Code")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/SDL")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/imgui")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/FSR2")

link_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Libs")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/*.cpp")

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/PhasmaPch.h")

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
        PE_VULKAN=1
        PE_DX12=0
)
target_compile_definitions(${PROJECT_NAME} PRIVATE "${ADDITIONAL_COMPILE_DEFINITIONS}")

set(ADDITIONAL_LIBRARY_DEPENDENCIES
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
target_link_libraries(${PROJECT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/DLLs/SDL2.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/DLLs/shaderc_shared.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/DLLs/dxcompiler.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Assets" "$<CONFIG>/Assets"
)