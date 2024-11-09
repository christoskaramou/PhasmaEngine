cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME Phasma)
project(${PROJECT_NAME} CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check the operating system
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
    set(PE_DEFS
        PE_LINUX
        PE_VULKAN
        _CONSOLE
    )
    add_compile_options(-fms-extensions)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
    set(PE_DEFS
        PE_WIN32
        PE_VULKAN
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        _CONSOLE
    )
else()
    error("Unsupported operating system")
    message(STATUS "Detected operating system: ${CMAKE_SYSTEM_NAME}")
endif()

if(MSVC)
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP${N}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP${N}")   
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Oi")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Oi") 
endif()
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}/MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo")

message(STATUS "CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO}")

# Phasma directories
set(PHASMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Phasma")
set(CODE_DIR "${PHASMA_DIR}/Code")
set(INCLUDE_DIR "${PHASMA_DIR}/Include")
set(LIBS_DIR "${PHASMA_DIR}/Libs")
set(DLLS_DIR "${PHASMA_DIR}/DLLs")
set(ASSETS_DIR "${PHASMA_DIR}/Assets")

# Include directories
include_directories("${PHASMA_DIR}")
include_directories("${CODE_DIR}")
include_directories("${INCLUDE_DIR}")
include_directories("${INCLUDE_DIR}/SDL2")
include_directories("${INCLUDE_DIR}/imgui")
include_directories("${INCLUDE_DIR}/FSR2")

# Link directories
link_directories("${LIBS_DIR}/dx")
link_directories("${LIBS_DIR}/fsr/Debug")
link_directories("${LIBS_DIR}/fsr/Release")
link_directories("${LIBS_DIR}/sdl2")
link_directories("${LIBS_DIR}/spirv")
link_directories("${LIBS_DIR}/vulkan")

# Collect .cpp files from the Include directory
file(GLOB_RECURSE INCLUDE_FILES CONFIGURE_DEPENDS "${INCLUDE_DIR}/*.cpp")

# Collect .cpp files from the Code directory
file(GLOB_RECURSE CODE_FILES CONFIGURE_DEPENDS "${CODE_DIR}/*.cpp")

# Combine the collected source files
set(SRC_FILES ${INCLUDE_FILES} ${CODE_FILES})

# Create the executable
add_executable(${PROJECT_NAME} ${SRC_FILES} ${PHASMA_DIR}/main.cpp)

# Precompiled headers
target_precompile_headers(${PROJECT_NAME} PRIVATE "${INCLUDE_DIR}/PhasmaPch.h")

# Compile Definitions
set(COMPILE_DEFS
    $<$<CONFIG:Debug>:_DEBUG PE_DEBUG>
    $<$<CONFIG:Release>:NDEBUG PE_RELEASE>
    $<$<CONFIG:MinSizeRel>:NDEBUG PE_MINSIZEREL>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG PE_RELWITHDEBINFO>
    ${PE_DEFS}
)
target_compile_definitions(${PROJECT_NAME} PRIVATE ${COMPILE_DEFS})

# Libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LIB_FILES
        $<$<CONFIG:Debug>:spirv-cross-cored>
        $<$<CONFIG:Release>:spirv-cross-core>
        $<$<CONFIG:MinsizeRel>:spirv-cross-core>
        $<$<CONFIG:RelWithDebInfo>:spirv-cross-core>
    )
else()
    set(LIB_FILES
        spirv-cross-core
    )
endif()
list(APPEND LIB_FILES 
    shaderc_shared
    dxcompiler
    SDL2main
    SDL2
    vulkan
)
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIB_FILES})

# Copy assets and DLLs to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLLS_DIR}/SDL2.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLLS_DIR}/shaderc_shared.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLLS_DIR}/dxcompiler.dll" "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSETS_DIR}" "$<CONFIG>/Assets"
)