cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0184)
    cmake_policy(SET CMP0184 NEW)
endif()

# Keep all MSVC targets, including fetched subprojects like Jolt, on the DLL CRT (/MD, /MDd)
# so they match the rest of the editor build and imported Windows dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(PhasmaEditor C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaEditor = ${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    # /MP enables multi-threaded compilation *within* each cl.exe invocation.
    # The build system (Ninja/MSBuild) already launches parallel jobs, so keep
    # /MP low to avoid N_jobs × N_threads memory explosion with large PCHs.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP4")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Oi /permissive-")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Oi /permissive-")
    # Add /bigobj to avoid MSVC "number of sections exceeded" (C1128) for large translation units
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /bigobj")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo")

set(PE_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/Libs")
set(PE_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/third_party")

# --- Tracy Profiler ---
option(PE_TRACY "Enable Tracy profiler integration" OFF)
if(PE_TRACY)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    add_subdirectory("${PE_THIRD_PARTY_DIR}/tracy")
endif()

# --- Jolt Physics ---
option(PE_PHYSICS "Enable Jolt Physics integration" ON)
if(PE_PHYSICS)
    include(FetchContent)
    set(BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
    set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
    set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    FetchContent_Declare(JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG        v5.2.0
        SOURCE_SUBDIR  Build
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(JoltPhysics)
    set(BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS_SAVED}" CACHE BOOL "" FORCE)

    if(TARGET Jolt)
        # MSVC: Jolt's own CMakeLists.txt hard-codes /RTC1 in Debug, which is
        # a fatal incompatibility with /O2 (D8016). Skip the optimization on
        # MSVC Debug — /O2 is only applied on non-MSVC toolchains.
        if(NOT MSVC)
            target_compile_options(Jolt PRIVATE $<$<CONFIG:Debug>:-O2>)
        endif()
    endif()
endif()

# --- MeshOptimizer (built from source with -fPIC) ---
include(FetchContent)
FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.21
    GIT_PROGRESS   TRUE
)
FetchContent_GetProperties(meshoptimizer)
if(NOT meshoptimizer_POPULATED)
    FetchContent_Populate(meshoptimizer)
    # Create a simple CMakeLists.txt for meshoptimizer since it doesn't have one
    file(WRITE "${meshoptimizer_SOURCE_DIR}/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.10)
project(meshoptimizer CXX)
file(GLOB SOURCES \"src/*.cpp\")
add_library(meshoptimizer STATIC \${SOURCES})
target_include_directories(meshoptimizer PUBLIC \"\${CMAKE_CURRENT_SOURCE_DIR}\")
set_target_properties(meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(meshoptimizer PRIVATE /W0)
else()
    target_compile_options(meshoptimizer PRIVATE -w)
endif()
")
    add_subdirectory(${meshoptimizer_SOURCE_DIR} ${meshoptimizer_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# --- Prebuilt Assimp (shared) ---
message(STATUS "[Phasma] Using prebuilt Assimp ...")
add_library(assimp SHARED IMPORTED GLOBAL)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(assimp PROPERTIES
        IMPORTED_IMPLIB "${PE_LIBS_DIR}/assimp/assimp.lib"
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/assimp-vc143-mt.dll"
    )
else()
    set_target_properties(assimp PROPERTIES
        IMPORTED_LOCATION "${PE_LIBS_DIR}/assimp/libassimp.so"
    )
endif()

# Add PhasmaCore subdirectory
add_subdirectory(PhasmaCore)

# Add PhasmaAgent subdirectory
add_subdirectory(PhasmaAgent)

# Add PhasmaWebGPU subdirectory (WebGPU C API layer over PhasmaCore)
option(PE_WEBGPU "Build PhasmaWebGPU WebGPU abstraction layer" ON)
if(PE_WEBGPU)
    add_subdirectory(PhasmaWebGPU)
endif()

# === PhasmaEditorModule + thin launcher ===
file(GLOB_RECURSE CODE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/*.cpp")
set(PE_EDITOR_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/third_party")

# --- Lua 5.4 (vendored, compiled from source) ---
file(GLOB LUA_SOURCES "${PE_EDITOR_THIRD_PARTY_DIR}/lua/*.c")
add_library(Lua STATIC ${LUA_SOURCES})
target_include_directories(Lua PUBLIC "${PE_EDITOR_THIRD_PARTY_DIR}/lua")
set_target_properties(Lua PROPERTIES LINKER_LANGUAGE C)
if(NOT MSVC)
    target_compile_options(Lua PRIVATE -w)
endif()
file(GLOB_RECURSE IMGUI_SRC_FILES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/*.cpp")
file(GLOB_RECURSE CACAO_SRC_FILES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/CACAO/*.cpp")
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets/*")
list(FILTER ASSET_FILES EXCLUDE REGEX "\\.obj$")
set_source_files_properties(${ASSET_FILES} PROPERTIES VS_TOOL_OVERRIDE "None")

# PhasmaEditorModule (SHARED) — all editor code lives in DLL
add_library(PhasmaEditorModule SHARED
    ${CODE_SRC_FILES}
    ${IMGUI_SRC_FILES}
    ${CACAO_SRC_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/App/EditorModule.cpp"
)
set_target_properties(PhasmaEditorModule PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(PhasmaEditorModule PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
endif()

if(MSVC)
    # Optimized Debug builds cannot combine /O1 with MSVC runtime checks.
    set_property(TARGET PhasmaEditorModule PROPERTY MSVC_RUNTIME_CHECKS "")
    target_compile_options(PhasmaEditorModule PRIVATE $<$<CONFIG:Debug>:/O1>)
else()
    target_compile_options(PhasmaEditorModule PRIVATE $<$<CONFIG:Debug>:-O1>)
endif()

target_compile_definitions(PhasmaEditorModule PRIVATE
    PE_EDITOR_MODULE_EXPORTS
    RAPIDJSON_HAS_STDSTRING=1
    RAPIDJSON_NOMEMBERITERATORCLASS
)
if(PE_PHYSICS AND TARGET Jolt)
    target_compile_definitions(PhasmaEditorModule PRIVATE PE_PHYSICS)
endif()

target_precompile_headers(PhasmaEditorModule PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")

target_include_directories(PhasmaEditorModule PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor"
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code"
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaAgent/third_party"
    "${PE_EDITOR_THIRD_PARTY_DIR}"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui"
    "${PE_EDITOR_THIRD_PARTY_DIR}/sol2"
)

target_link_libraries(PhasmaEditorModule PRIVATE
    PhasmaCore
    PhasmaAgent
    assimp
    meshoptimizer
    Lua
)
if(PE_PHYSICS AND TARGET Jolt)
    target_link_libraries(PhasmaEditorModule PRIVATE Jolt)
endif()

option(PE_AUDIO "Enable miniaudio integration" ON)
if(PE_AUDIO)
    target_include_directories(PhasmaEditorModule PRIVATE "${PE_EDITOR_THIRD_PARTY_DIR}/miniaudio")
    target_compile_definitions(PhasmaEditorModule PRIVATE PE_AUDIO)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaEditorModule PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# PhasmaEditor (thin launcher EXE)
add_executable(PhasmaEditor "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/main.cpp")

target_precompile_headers(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")

target_include_directories(PhasmaEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor"
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code"
)

target_link_libraries(PhasmaEditor PRIVATE PhasmaCore)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(PhasmaEditor PRIVATE dl)
    set_target_properties(PhasmaEditor PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

add_dependencies(PhasmaEditor PhasmaEditorModule)

# Copy assets to build directory (re-runs when assets change)
set(ASSETS_COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/AssetsCopy.stamp")
set(PHASMAEDITOR_RUNTIME_DIR "$<IF:$<CONFIG:Debug>,${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG},$<IF:$<CONFIG:Release>,${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE},$<IF:$<CONFIG:RelWithDebInfo>,${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO},${CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL}>>>")
add_custom_command(
    OUTPUT "${ASSETS_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets" "${PHASMAEDITOR_RUNTIME_DIR}/Assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_COPY_STAMP}"
    DEPENDS ${ASSET_FILES}
    COMMENT "[Phasma] Copying assets to build directory"
)
add_custom_target(PhasmaEditorAssets ALL DEPENDS "${ASSETS_COPY_STAMP}")
add_dependencies(PhasmaEditor PhasmaEditorAssets)

# Copy DLLs for Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_command(TARGET PhasmaEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PhasmaCore>" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PhasmaEditorModule>" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/dxcompiler.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/shaderc_shared.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/SDL2.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/assimp-vc143-mt.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/libssl-3-x64.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/libcrypto-3-x64.dll" "$<TARGET_FILE_DIR:PhasmaEditor>")
else()
    add_custom_command(TARGET PhasmaEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PhasmaCore>" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PhasmaEditorModule>" "$<TARGET_FILE_DIR:PhasmaEditor>"
    )
endif()
