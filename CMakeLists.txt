cmake_minimum_required(VERSION 3.10)
project(Phasma CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "Phasma = ${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP${N}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP${N}")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Oi /permissive-")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Oi /permissive-") 
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}/MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo")

# Add PhasmaCore subdirectory
add_subdirectory(PhasmaCore)

# Phasma executable
file(GLOB_RECURSE CODE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Code/*.cpp")
file(GLOB_RECURSE INCLUDE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/*.cpp")
add_executable(Phasma ${CODE_SRC_FILES} ${INCLUDE_SRC_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/Phasma/main.cpp)

# Phasma precompiled headers
target_precompile_headers(Phasma REUSE_FROM PhasmaCore)

# Phasma include directories from PhasmaCore in build directory
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/PhasmaCore/Include")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/PhasmaCore/Include/third_party")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/PhasmaCore/Include/third_party/SDL2")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/PhasmaCore/Include/third_party/imgui")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/PhasmaCore/Include/third_party/FSR2")

# Phasma include directories
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Code")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include")
target_include_directories(Phasma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Include/imgui")

# Phasma link directories
target_link_directories(Phasma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Libs/fsr/Debug")
target_link_directories(Phasma PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Libs/fsr/Release")

# Libraries
target_link_libraries(Phasma PRIVATE PhasmaCore)

# target_compile_options(Phasma PRIVATE -fsanitize=address)
# target_link_options(Phasma PRIVATE -fsanitize=address)

# Copy assets to build directory
add_custom_command(TARGET Phasma POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Phasma/Assets" "$<TARGET_FILE_DIR:Phasma>/Assets")
