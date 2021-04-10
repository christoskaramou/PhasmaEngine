cmake_minimum_required(VERSION 3.2)
set(PROJECT_NAME VulkanMonkey)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message("CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message("CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")
message("CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/Include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/Include/imgui")

link_directories("${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/Libs")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/*.cpp")

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/Include/vulkanPCH.h")

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

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/objects" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/Scripts" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/shaders" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/imgui.ini" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/SDL2.dll" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMonkey/shaderc_shared.dll" DESTINATION ${CMAKE_BINARY_DIR})