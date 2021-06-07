cmake_minimum_required(VERSION 3.2)
set(PROJECT_NAME Phasma)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(MSVC_COMPILE_FLAGS "/MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MSVC_COMPILE_FLAGS}" )
    message("COMPILER = MSVC")
endif()

message("CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message("CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")
message("CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/imgui")

link_directories("${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Libs")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/*.cpp")

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/PhasmaPch.h")

set(ADDITIONAL_COMPILE_DEFINITIONS
        "$<$<CONFIG:Debug>:"
            "WIN32;"
            "_DEBUG"
        ">"
        "$<$<CONFIG:Release>:"
            "NDEBUG"
        ">"
        "_CONSOLE;"
        "UNICODE;"
        "_UNICODE"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE "${ADDITIONAL_COMPILE_DEFINITIONS}")

set(ADDITIONAL_LIBRARY_DEPENDENCIES
    "$<$<CONFIG:Debug>:"
        "spirv-cross-cored;"
        "GLTFSDKd"
    ">"
    "$<$<CONFIG:Release>:"
        "spirv-cross-core;"
        "GLTFSDK"
    ">"
    "shaderc_shared;"
    "SDL2main;"
    "SDL2;"
    "vulkan-1"
)
target_link_libraries(${PROJECT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/D3D12.dll" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/imgui.ini" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/SDL2.dll" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/shaderc_shared.dll" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Assets" DESTINATION ${CMAKE_BINARY_DIR})

file(WRITE "${CMAKE_BINARY_DIR}/AssetsRoot" "Assets root: Assets/")