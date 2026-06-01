cmake_minimum_required(VERSION 3.22)

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
set(PE_SHARED_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaRuntime/third_party")
set(PE_EDITOR_THIRD_PARTY_DIR "${PE_SHARED_THIRD_PARTY_DIR}")
set(PE_RUNTIME_ASSETS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets")
set(PE_EDITOR_ASSETS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/EditorAssets")

set(PE_DESKTOP_TARGETS_DEFAULT ON)
set(PE_RUNTIME_SHADER_COMPILER_DEFAULT ON)
set(PE_ASSIMP_DEFAULT ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PE_DESKTOP_TARGETS_DEFAULT OFF)
    set(PE_RUNTIME_SHADER_COMPILER_DEFAULT OFF)
    set(PE_ASSIMP_DEFAULT OFF)
endif()

option(PE_BUILD_EDITOR "Build the editor host and editor module" ${PE_DESKTOP_TARGETS_DEFAULT})
option(PE_BUILD_LAUNCHER "Build the desktop launcher" ${PE_DESKTOP_TARGETS_DEFAULT})
option(PE_BUILD_MCP "Build the MCP server library" ${PE_DESKTOP_TARGETS_DEFAULT})
option(PE_BUILD_PLAYER "Build the standalone player host" ON)
option(PE_WEBGPU "Build PhasmaWebGPU WebGPU abstraction layer" ${PE_DESKTOP_TARGETS_DEFAULT})
option(PE_ENABLE_ASSIMP "Enable Assimp-backed runtime model file loading" ${PE_ASSIMP_DEFAULT})
option(PE_ENABLE_RUNTIME_SHADER_COMPILER "Enable runtime shader compilation from source files" ${PE_RUNTIME_SHADER_COMPILER_DEFAULT})

set(PE_SDL2_INCLUDE_DIR "${PE_THIRD_PARTY_DIR}/SDL2")
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PE_ANDROID_SDL2_ROOT "" CACHE PATH "Full SDL2 source tree with Android CMake/Java glue")
    set(PE_ANDROID_SPIRV_CROSS_ROOT "" CACHE PATH "Full SPIRV-Cross source tree with CMake support")
    set(PE_ANDROID_CONFIG_ERRORS)
    if(NOT PE_ANDROID_SDL2_ROOT)
        list(APPEND PE_ANDROID_CONFIG_ERRORS "PE_ANDROID_SDL2_ROOT=<full SDL2 source tree>")
    endif()
    if(NOT PE_ANDROID_SPIRV_CROSS_ROOT)
        list(APPEND PE_ANDROID_CONFIG_ERRORS "PE_ANDROID_SPIRV_CROSS_ROOT=<full SPIRV-Cross source tree>")
    endif()
    if(PE_ANDROID_CONFIG_ERRORS)
        string(REPLACE ";" "\n  - " PE_ANDROID_CONFIG_ERROR_TEXT "${PE_ANDROID_CONFIG_ERRORS}")
        message(FATAL_ERROR "Android builds require:\n  - ${PE_ANDROID_CONFIG_ERROR_TEXT}")
    endif()

    cmake_path(ABSOLUTE_PATH PE_ANDROID_SDL2_ROOT NORMALIZE)
    cmake_path(ABSOLUTE_PATH PE_ANDROID_SPIRV_CROSS_ROOT NORMALIZE)
    if(NOT EXISTS "${PE_ANDROID_SDL2_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${PE_ANDROID_SDL2_ROOT}/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java")
        message(FATAL_ERROR "PE_ANDROID_SDL2_ROOT must point to a full SDL2 source tree with android-project")
    endif()
    if(NOT EXISTS "${PE_ANDROID_SPIRV_CROSS_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "PE_ANDROID_SPIRV_CROSS_ROOT must point to a full SPIRV-Cross source tree")
    endif()
    set(PE_SDL2_INCLUDE_DIR "${PE_ANDROID_SDL2_ROOT}/include")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PE_ANDROID_SDL2_ROOT}" "${CMAKE_BINARY_DIR}/SDL2" EXCLUDE_FROM_ALL)

    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PE_ANDROID_SPIRV_CROSS_ROOT}" "${CMAKE_BINARY_DIR}/SPIRV-Cross" EXCLUDE_FROM_ALL)
endif()

include(cmake/PhasmaRuntimeLayout.cmake)
pe_add_root_runtime_dependencies_target()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PE_CORE_LINK_DIRS)
else()
    set(PE_CORE_LINK_DIRS
        "${PE_LIBS_DIR}/dx"
        "${PE_LIBS_DIR}/SDL2"
        "${PE_LIBS_DIR}/spirv"
        "${PE_LIBS_DIR}/vulkan"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PE_SPIRV_CROSS_CORE_LIB
        $<$<CONFIG:Debug>:spirv-cross-cored>
        $<$<CONFIG:Release>:spirv-cross-core>
        $<$<CONFIG:MinSizeRel>:spirv-cross-core>
        $<$<CONFIG:RelWithDebInfo>:spirv-cross-core>
    )
else()
    set(PE_SPIRV_CROSS_CORE_LIB spirv-cross-core)
endif()

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
cmake_minimum_required(VERSION 3.22)
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
if(PE_ENABLE_ASSIMP)
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
else()
    message(STATUS "[Phasma] Assimp disabled; runtime model file loading is unavailable")
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

if(PE_BUILD_PLAYER)
    # Standalone player host over PhasmaRuntime.
    add_subdirectory(PhasmaPlayer)
endif()

if(PE_BUILD_MCP)
    # Engine-agnostic MCP server library (protocol + transport + optional codebase indexer).
    # The codebase indexer (BM25 / CodebaseIndexer / CodebaseContext) is included by default
    # inside the same target; pass -DPE_PMCP_CODEBASE=OFF to strip it for minimal builds.
    add_subdirectory(PhasmaMCP)
endif()

# Add PhasmaWebGPU subdirectory (WebGPU C API layer over PhasmaCore)
if(PE_WEBGPU)
    add_subdirectory(PhasmaWebGPU)
endif()

# === PhasmaEditorModule + host executables ===
if(PE_BUILD_EDITOR)
file(GLOB_RECURSE CODE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/*.cpp")
file(GLOB_RECURSE IMGUI_SRC_FILES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/*.cpp")
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${PE_RUNTIME_ASSETS_SOURCE_DIR}/*")
list(FILTER ASSET_FILES EXCLUDE REGEX "\\.obj$")
file(GLOB_RECURSE EDITOR_ASSET_FILES CONFIGURE_DEPENDS "${PE_EDITOR_ASSETS_SOURCE_DIR}/*")
set(PE_SHARED_IMGUI_BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends/ImGuiRuntimeUiBackend.cpp"
)
if(WIN32)
    list(APPEND PE_SHARED_IMGUI_BACKEND_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends/imgui_impl_dx12.cpp"
    )
endif()
set_source_files_properties(${ASSET_FILES} PROPERTIES VS_TOOL_OVERRIDE "None")

# PhasmaEditorModule (SHARED) — all editor code lives in DLL
add_library(PhasmaEditorModule SHARED
    ${CODE_SRC_FILES}
    ${IMGUI_SRC_FILES}
    ${PE_SHARED_IMGUI_BACKEND_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/App/EditorModule.cpp"
)
set_target_properties(PhasmaEditorModule PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "PhasmaEditorModule2" # TEMP (Bitdefender delete-pending lock workaround): revert to default name once the lock clears
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
    "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends"
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
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaEditorModule PRIVATE ${PE_CORE_LINK_DIRS})
endif()

target_link_libraries(PhasmaEditorModule PRIVATE
    PhasmaCore
    meshoptimizer
    Lua
    ${PE_SPIRV_CROSS_CORE_LIB}
    SDL2
    SDL2main
    vulkan
)
# $<LINK_LIBRARY:WHOLE_ARCHIVE> needs CMake 3.24; the project minimum is 3.22 (Android
# NDK toolchain), so fall back to the linker-native whole-archive flags on older CMake.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(PhasmaEditorModule PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>")
elseif(MSVC)
    target_link_libraries(PhasmaEditorModule PRIVATE PhasmaRuntime)
    target_link_options(PhasmaEditorModule PRIVATE "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:PhasmaRuntime>")
else()
    target_link_libraries(PhasmaEditorModule PRIVATE -Wl,--whole-archive PhasmaRuntime -Wl,--no-whole-archive)
endif()
if(PE_PHYSICS AND TARGET Jolt)
    target_link_libraries(PhasmaEditorModule PRIVATE Jolt)
endif()
if(TARGET PhasmaMCP)
    target_link_libraries(PhasmaEditorModule PRIVATE PhasmaMCP)
endif()
if(TARGET assimp)
    target_link_libraries(PhasmaEditorModule PRIVATE assimp)
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
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaEditor PRIVATE ${PE_CORE_LINK_DIRS})
endif()
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
endif()

if(PE_BUILD_LAUNCHER)
# PhasmaLauncher (pre-launch target/sample/backend selector)
add_executable(PhasmaLauncher "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaLauncher/main.cpp")

target_precompile_headers(PhasmaLauncher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")
target_include_directories(PhasmaLauncher PRIVATE
    "${PE_THIRD_PARTY_DIR}/rapidjson/include"
)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaLauncher PRIVATE ${PE_CORE_LINK_DIRS})
endif()
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

if(TARGET PhasmaEditor)
    add_dependencies(PhasmaLauncher PhasmaEditor)
endif()
if(TARGET PhasmaPlayer)
    add_dependencies(PhasmaLauncher PhasmaPlayer)
endif()
pe_depend_on_root_runtime_dependencies(PhasmaLauncher)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Copy runtime assets to build directory (re-runs when assets change)
    file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${PE_RUNTIME_ASSETS_SOURCE_DIR}/*")
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
    if(TARGET PhasmaEditor)
        add_dependencies(PhasmaEditor PhasmaRuntimeAssets)
    endif()
    if(TARGET PhasmaLauncher)
        add_dependencies(PhasmaLauncher PhasmaRuntimeAssets)
    endif()
    if(TARGET PhasmaPlayer)
        add_dependencies(PhasmaPlayer PhasmaRuntimeAssets)
    endif()
endif()

if(PE_BUILD_EDITOR)
# Editor-only assets (Icons, Fonts, SplashScreen-free chrome, Profiler, Temp, Images).
# Copied next to the editor exe; not shipped with the standalone player.
file(GLOB_RECURSE EDITOR_ASSET_FILES CONFIGURE_DEPENDS "${PE_EDITOR_ASSETS_SOURCE_DIR}/*")
set(EDITOR_ASSETS_COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/PhasmaEditorAssets-$<CONFIG>.stamp")
add_custom_command(
    OUTPUT "${EDITOR_ASSETS_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles" "${PHASMA_CONFIG_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PE_EDITOR_ASSETS_SOURCE_DIR}" "${PHASMA_CONFIG_OUTPUT_DIR}/EditorAssets"
    COMMAND ${CMAKE_COMMAND} -E touch "${EDITOR_ASSETS_COPY_STAMP}"
    DEPENDS ${EDITOR_ASSET_FILES}
    COMMENT "[Phasma] Copying editor assets to build directory"
)
add_custom_target(PhasmaEditorAssets ALL DEPENDS "${EDITOR_ASSETS_COPY_STAMP}")
if(TARGET PhasmaEditor)
    add_dependencies(PhasmaEditor PhasmaEditorAssets)
endif()
endif()

pe_install_runtime_layout()
