cmake_minimum_required(VERSION 3.10)
project(PhasmaCore CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaCore = ${CMAKE_CURRENT_SOURCE_DIR}")

# Check the operating system
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
    set(PE_DEFS
        PE_LINUX
        _CONSOLE
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
    set(PE_DEFS
        PE_WIN32
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

set(PE_DEFS ${PE_DEFS} PE_TRACK_RESOURCES)

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

# Collect all .cpp and .c files from the source directories
file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Code/Base/*.cpp")
file(GLOB_RECURSE ECS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Code/ECS/*.cpp")
file(GLOB_RECURSE API_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Code/API/*.cpp")
file(GLOB_RECURSE VMA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/*.cpp")

# Add PhasmaCore static library
add_library(PhasmaCore STATIC
        ${BASE_SOURCES}
        ${ECS_SOURCES}
        ${API_SOURCES}
        ${VMA_SOURCES}
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # MS-style declspec/anonymous unions etc., without changing core language types
    target_compile_options(PhasmaCore PUBLIC -fms-extensions)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(PhasmaCore PUBLIC -fms-extensions)
endif()

# Precompiled headers
target_precompile_headers(PhasmaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/pch/PhasmaPch.h")

# Set the include directories
target_include_directories(PhasmaCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Code
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dxc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/RenderDoc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shaderc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv_cross
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vk_video
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vk_video
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2
)

# Add Assimp include directory if available
if(TARGET assimp)
    target_include_directories(PhasmaCore PUBLIC ${assimp_SOURCE_DIR}/include)
endif()

# Link directories
# cannot find this when running from main CMakeLists.txt
target_link_directories(PhasmaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Libs/dx")
target_link_directories(PhasmaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Libs/SDL2")
target_link_directories(PhasmaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Libs/spirv")
target_link_directories(PhasmaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Libs/vulkan")

# Compile Definitions
set(COMPILE_DEFS
    $<$<CONFIG:Debug>:_DEBUG PE_DEBUG>
    $<$<CONFIG:Release>:NDEBUG PE_RELEASE>
    $<$<CONFIG:MinSizeRel>:NDEBUG PE_MINSIZEREL>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG PE_RELWITHDEBINFO>
    ${PE_DEFS}
)
target_compile_definitions(PhasmaCore PUBLIC ${COMPILE_DEFS})

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
target_link_libraries(PhasmaCore PUBLIC ${LIB_FILES})

# Copy headers into the corresponding folder in PhasmaCore/include/
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Code/
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Include/
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl" PATTERN "*.hlsl" PATTERN "*.spv")

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Include/third_party
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")

# Copy shader compilers for windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Copy dlls to build directory
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/DLLs/
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/DLLs
    )
endif()