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

if(TARGET MaterialXGenSlang)
    target_link_libraries(PhasmaCook PRIVATE MaterialXGenSlang MaterialXFormat)
    target_compile_definitions(PhasmaCook PRIVATE PE_MATERIALX)
    target_sources(PhasmaCook PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Code/MaterialX/MaterialXGen.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Code/MaterialX/PeSurfaceNodeSlang.cpp")

    # Generation reads MaterialX's library data (<exe>/MaterialX/libraries) and PE's overrides at cook time.
    add_custom_command(TARGET PhasmaCook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PE_MATERIALX_LIBRARIES_DIR}"
                "$<TARGET_FILE_DIR:PhasmaCook>/MaterialX/libraries"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides"
                "$<TARGET_FILE_DIR:PhasmaCook>/MaterialX/pe_overrides")

    # PE's default OpenPBR module (every untextured OpenPBR material shares it), generated into the runtime assets
    # on every change. The overrides are copied first so a changed override regenerates with itself.
    file(GLOB_RECURSE PE_MATERIALX_OVERRIDE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/*")
    set(PE_OPENPBR_GENERATED_DIR "${PHASMA_CONFIG_OUTPUT_DIR}/RuntimeAssets/Shaders/MaterialX/Generated")

    add_custom_command(
        OUTPUT "${PE_OPENPBR_GENERATED_DIR}/PeOpenPBRDefault.slang"
               "${PE_OPENPBR_GENERATED_DIR}/PeOpenPBRDefault.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides"
                "$<TARGET_FILE_DIR:PhasmaCook>/MaterialX/pe_overrides"
        COMMAND "$<TARGET_FILE:PhasmaCook>" --mtlx
                "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/pe_default_openpbr.mtlx"
                "${PE_OPENPBR_GENERATED_DIR}"
        DEPENDS PhasmaCook ${PE_MATERIALX_OVERRIDE_FILES}
        COMMENT "[Phasma] Generating the default OpenPBR module (PhasmaCook --mtlx)"
        VERBATIM)

    add_custom_target(PeOpenPBRDefaultModule ALL DEPENDS "${PE_OPENPBR_GENERATED_DIR}/PeOpenPBRDefault.slang")
    add_dependencies(PeOpenPBRDefaultModule PhasmaRuntimeAssets)
endif()

pe_link_project_native(PhasmaCook)
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
