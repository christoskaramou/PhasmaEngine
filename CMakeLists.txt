cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0184)
    cmake_policy(SET CMP0184 NEW)
endif()

# Keep all MSVC targets, including fetched subprojects like Jolt, on the DLL CRT (/MD, /MDd)
# so they match the rest of the editor build and imported Windows dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Static libs linked into PhasmaEditorModule need PIC on ELF targets.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(PhasmaEngine C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaEngine = ${CMAKE_CURRENT_SOURCE_DIR}")

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
set(PE_SHARED_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/third_party")
set(PE_EDITOR_THIRD_PARTY_DIR "${PE_SHARED_THIRD_PARTY_DIR}")
set(PE_RUNTIME_ASSETS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets")
include(cmake/PhasmaRuntimeLayout.cmake)
pe_add_root_runtime_dependencies_target()

set(PE_CORE_LINK_DIRS
    "${PE_LIBS_DIR}/dx"
    "${PE_LIBS_DIR}/SDL2"
    "${PE_LIBS_DIR}/spirv"
    "${PE_LIBS_DIR}/vulkan"
)

set(PE_SPIRV_CROSS_CORE_LIB
    $<$<CONFIG:Debug>:spirv-cross-cored>
    $<$<CONFIG:Release>:spirv-cross-core>
    $<$<CONFIG:MinSizeRel>:spirv-cross-core>
    $<$<CONFIG:RelWithDebInfo>:spirv-cross-core>
)

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

option(PE_AUDIO "Enable miniaudio integration" ON)

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
cmake_minimum_required(VERSION 3.24)
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

# --- Lua 5.4 (vendored, compiled from source) ---
file(GLOB LUA_SOURCES "${PE_SHARED_THIRD_PARTY_DIR}/lua/*.c")
add_library(Lua STATIC ${LUA_SOURCES})
target_include_directories(Lua PUBLIC "${PE_SHARED_THIRD_PARTY_DIR}/lua")
set_target_properties(Lua PROPERTIES LINKER_LANGUAGE C)
if(NOT MSVC)
    target_compile_options(Lua PRIVATE -w)
endif()

# Shared project/runtime contract used by editor today and future player hosts.
add_subdirectory(PhasmaRuntime)

# Standalone player host over PhasmaRuntime.
add_subdirectory(PhasmaPlayer)

# Engine-agnostic MCP server library (protocol + transport + optional codebase indexer).
# The codebase indexer (BM25 / CodebaseIndexer / CodebaseContext) is included by default
# inside the same target; pass -DPE_PMCP_CODEBASE=OFF to strip it for minimal builds.
add_subdirectory(PhasmaMCP)

# Add PhasmaWebGPU subdirectory (WebGPU C API layer over PhasmaCore)
option(PE_WEBGPU "Build PhasmaWebGPU WebGPU abstraction layer" ON)
if(PE_WEBGPU)
    add_subdirectory(PhasmaWebGPU)
endif()

# === PhasmaEditorModule + thin launcher ===
file(GLOB_RECURSE CODE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/*.cpp")
file(GLOB_RECURSE IMGUI_SRC_FILES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/*.cpp")
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${PE_RUNTIME_ASSETS_SOURCE_DIR}/*")
list(FILTER ASSET_FILES EXCLUDE REGEX "\\.obj$")
set_source_files_properties(${ASSET_FILES} PROPERTIES VS_TOOL_OVERRIDE "None")

# PhasmaEditorModule (SHARED) — all editor code lives in DLL
add_library(PhasmaEditorModule SHARED
    ${CODE_SRC_FILES}
    ${IMGUI_SRC_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/App/EditorModule.cpp"
)
set_target_properties(PhasmaEditorModule PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaMCP/third_party"
    "${PE_THIRD_PARTY_DIR}/assimp/include"
    "${PE_THIRD_PARTY_DIR}/meshoptimizer"
    "${PE_THIRD_PARTY_DIR}/rapidjson/include"
    "${PE_THIRD_PARTY_DIR}/spirv_cross"
    "${PE_THIRD_PARTY_DIR}/stb"
    "${PE_THIRD_PARTY_DIR}"
    "${PE_THIRD_PARTY_DIR}/vma"
    "${PE_THIRD_PARTY_DIR}/vulkan"
    "${PE_SHARED_THIRD_PARTY_DIR}"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui"
    "${PE_SHARED_THIRD_PARTY_DIR}/sol2"
)
target_link_directories(PhasmaEditorModule PRIVATE ${PE_CORE_LINK_DIRS})

target_link_libraries(PhasmaEditorModule PRIVATE
    PhasmaCore
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>"
    PhasmaMCP
    assimp
    meshoptimizer
    Lua
    ${PE_SPIRV_CROSS_CORE_LIB}
    SDL2
    SDL2main
    vulkan
)
if(PE_PHYSICS AND TARGET Jolt)
    target_link_libraries(PhasmaEditorModule PRIVATE Jolt)
endif()
if(WIN32)
    target_link_libraries(PhasmaEditorModule PRIVATE d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)
endif()

if(PE_AUDIO)
    target_include_directories(PhasmaEditorModule PRIVATE "${PE_SHARED_THIRD_PARTY_DIR}/miniaudio")
    target_compile_definitions(PhasmaEditorModule PRIVATE PE_AUDIO)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaEditorModule PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()
pe_depend_on_root_runtime_dependencies(PhasmaEditorModule)

# PhasmaEditor (editor host EXE)
add_executable(PhasmaEditor "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/main.cpp")

target_precompile_headers(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")

set(PHASMA_EDITOR_HOST_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor"
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code"
)

target_include_directories(PhasmaEditor PRIVATE ${PHASMA_EDITOR_HOST_INCLUDES})
target_link_directories(PhasmaEditor PRIVATE ${PE_CORE_LINK_DIRS})
target_link_libraries(PhasmaEditor PRIVATE PhasmaCore PhasmaRuntime SDL2 SDL2main)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(PhasmaEditor PRIVATE dl)
    set_target_properties(PhasmaEditor PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

add_dependencies(PhasmaEditor PhasmaEditorModule)
pe_depend_on_root_runtime_dependencies(PhasmaEditor)

# PhasmaLauncher (pre-launch target/sample/backend selector)
add_executable(PhasmaLauncher "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Launcher.cpp")

target_precompile_headers(PhasmaLauncher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")
target_include_directories(PhasmaLauncher PRIVATE
    ${PHASMA_EDITOR_HOST_INCLUDES}
    "${PE_THIRD_PARTY_DIR}/rapidjson/include"
)
target_link_directories(PhasmaLauncher PRIVATE ${PE_CORE_LINK_DIRS})
target_link_libraries(PhasmaLauncher PRIVATE PhasmaCore PhasmaRuntime SDL2 SDL2main)
target_sources(PhasmaLauncher PRIVATE
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_impl_sdl2.cpp"
)
target_include_directories(PhasmaLauncher PRIVATE "${PE_EDITOR_THIRD_PARTY_DIR}/imgui")
if(WIN32)
    target_link_libraries(PhasmaLauncher PRIVATE comdlg32.lib shell32.lib ole32.lib)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaLauncher PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

add_dependencies(PhasmaLauncher PhasmaEditor PhasmaPlayer)
pe_depend_on_root_runtime_dependencies(PhasmaLauncher)

# Copy runtime assets to build directory (re-runs when assets change)
set(ASSETS_COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/PhasmaRuntimeAssets-$<CONFIG>.stamp")
add_custom_command(
    OUTPUT "${ASSETS_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles" "${PHASMA_CONFIG_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PE_RUNTIME_ASSETS_SOURCE_DIR}" "${PHASMA_CONFIG_OUTPUT_DIR}/Assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_COPY_STAMP}"
    DEPENDS ${ASSET_FILES}
    COMMENT "[Phasma] Copying assets to build directory"
)
add_custom_target(PhasmaRuntimeAssets ALL DEPENDS "${ASSETS_COPY_STAMP}")
add_custom_target(PhasmaEditorAssets DEPENDS "${ASSETS_COPY_STAMP}")
add_dependencies(PhasmaEditor PhasmaRuntimeAssets)
add_dependencies(PhasmaLauncher PhasmaRuntimeAssets)
add_dependencies(PhasmaPlayer PhasmaRuntimeAssets)

pe_install_runtime_layout()
