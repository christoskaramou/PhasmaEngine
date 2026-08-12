cmake_minimum_required(VERSION 3.22)
project(PhasmaCook CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaCook = ${CMAKE_CURRENT_SOURCE_DIR}")

# Desktop mesh-cook tool. The ONLY engine target that compiles ModelAssetAssimp / links Assimp.
# PhasmaRuntime is linked normally (NOT whole-archive) so the linker pulls only the cook closure
# (ModelAsset / ModelAssetCooked / RHI / RuntimeHost) and not the renderer TUs.
#
# CookUI.cpp adds an optional interactive window (no-args mode) using SDL_Renderer + Dear ImGui,
# wholly separate from the engine's Vulkan RHI (the headless cook is delegated to this same exe).
set(PHASMACOOK_IMGUI_SOURCES
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_impl_sdl2.cpp"
)
add_executable(PhasmaCook
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CookUI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/Scene/ModelAssetAssimp.cpp"
    # Real (assimp-backed) AnimationImporter: PhasmaRuntime only carries the PE_ENABLE_ASSIMP=off
    # stubs, so the cook must compile the TU itself or every cooked model gets 0 bones / 0 clips.
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/Animation/AnimationImporter.cpp"
    ${PHASMACOOK_IMGUI_SOURCES}
)

target_precompile_headers(PhasmaCook PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
# Build the vendored Dear ImGui TUs standalone (no engine PCH), like PhasmaLauncher does.
set_source_files_properties(${PHASMACOOK_IMGUI_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_compile_definitions(PhasmaCook PRIVATE PE_ENABLE_ASSIMP)
# SDL_endian.h hand-defines _m_prefetch under clang-cl to dodge a winnt.h clash, guarded only by
# clang's own __PRFCHWINTRIN_H include guard. The AVX flags Jolt propagates through PhasmaRuntime
# make clang treat _m_prefetch as a builtin, so SDL's redefinition collides. Pre-defining the guard
# makes SDL skip its definition and use the builtin (the escape hatch SDL's guard is designed for).
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(PhasmaCook PRIVATE __PRFCHWINTRIN_H)
endif()

target_include_directories(PhasmaCook PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Code"
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/assimp/include"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/meshoptimizer"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/rapidjson/include"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/stb"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/vma"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/vulkan"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party"
    "${PE_SHARED_THIRD_PARTY_DIR}"
)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaCook PRIVATE ${PE_CORE_LINK_DIRS})
endif()

target_link_libraries(PhasmaCook PRIVATE
    PhasmaCore
    PhasmaRuntime
    meshoptimizer
    vulkan
    SDL2
    SDL2main
)
if(TARGET assimp)
    target_link_libraries(PhasmaCook PRIVATE assimp)
endif()
if(WIN32)
    target_link_libraries(PhasmaCook PRIVATE d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib shell32.lib)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaCook PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(COMMAND pe_depend_on_root_runtime_dependencies)
    pe_depend_on_root_runtime_dependencies(PhasmaCook)
endif()
