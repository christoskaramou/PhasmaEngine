cmake_minimum_required(VERSION 3.10)
project(PhasmaEditor CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaEditor = ${CMAKE_CURRENT_SOURCE_DIR}")

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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo")

# Add PhasmaCore subdirectory
add_subdirectory(PhasmaCore)

# PhasmaEditor executable
file(GLOB_RECURSE CODE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/*.cpp")
file(GLOB_RECURSE INCLUDE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Include/*.cpp")
add_executable(PhasmaEditor ${CODE_SRC_FILES} ${INCLUDE_SRC_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/main.cpp)

# PhasmaEditor precompiled headers
target_precompile_headers(PhasmaEditor REUSE_FROM PhasmaCore)

# PhasmaEditor include directories
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor")
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code")
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Include")
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Include/imgui")
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Include/assimp")

# PhasmaEditor link directories
# target_link_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Libs/fsr/Debug")
# target_link_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Libs/fsr/Release")

# Libraries
target_link_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Libs/assimp")
set(LIB_FILES
    $<$<CONFIG:Debug>:assimp-vc143-mtd>
    $<$<CONFIG:Release>:assimp-vc143-mt>
    $<$<CONFIG:MinsizeRel>:assimp-vc143-mt>
    $<$<CONFIG:RelWithDebInfo>:assimp-vc143-mt>
    zlibstatic
)
target_link_libraries(PhasmaEditor PRIVATE PhasmaCore ${LIB_FILES})

# target_compile_options(PhasmaEditor PRIVATE -fsanitize=address)
# target_link_options(PhasmaEditor PRIVATE -fsanitize=address)

# Copy assets to build directory
add_custom_command(TARGET PhasmaEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets" "$<TARGET_FILE_DIR:PhasmaEditor>/Assets"
)

# Copy shader compilers for windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_command(TARGET PhasmaEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/PhasmaCore/DLLs/dxcompiler.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/PhasmaCore/DLLs/shaderc_shared.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
    )
endif()
