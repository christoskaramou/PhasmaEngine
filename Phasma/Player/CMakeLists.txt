cmake_minimum_required(VERSION 3.22)
project(PhasmaPlayer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaPlayer = ${CMAKE_CURRENT_SOURCE_DIR}")

set(PHASMAPLAYER_RUNTIME_UI_SOURCES
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends/ImGuiRuntimeUiBackend.cpp"
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends/ImGuiRuntimeUiBackend.h"
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends/ImGuiRuntimeUiStyle.h"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_impl_sdl2.cpp"
)
if(NOT PE_WEB)
    list(APPEND PHASMAPLAYER_RUNTIME_UI_SOURCES
        "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends/imgui_impl_vulkan.cpp"
    )
endif()

if(WIN32)
    list(APPEND PHASMAPLAYER_RUNTIME_UI_SOURCES
        "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends/imgui_impl_dx12.cpp"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_library(PhasmaPlayer SHARED
        "${CMAKE_CURRENT_SOURCE_DIR}/android_main.cpp"
        ${PHASMAPLAYER_RUNTIME_UI_SOURCES}
    )
    set_target_properties(PhasmaPlayer PROPERTIES OUTPUT_NAME "main")
else()
    add_executable(PhasmaPlayer
        "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
        ${PHASMAPLAYER_RUNTIME_UI_SOURCES}
    )
endif()

target_precompile_headers(PhasmaPlayer PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_include_directories(PhasmaPlayer PRIVATE
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/vma"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/vulkan"
)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaPlayer PRIVATE ${PE_CORE_LINK_DIRS})
endif()
target_link_libraries(PhasmaPlayer PRIVATE PhasmaCore)
pe_link_project_native(PhasmaPlayer)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(PhasmaPlayer PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>")
elseif(MSVC)
    target_link_libraries(PhasmaPlayer PRIVATE PhasmaRuntime)
    target_link_options(PhasmaPlayer PRIVATE "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:PhasmaRuntime>")
else()
    target_link_libraries(PhasmaPlayer PRIVATE -Wl,--whole-archive PhasmaRuntime -Wl,--no-whole-archive)
endif()
if(PE_WEB)
    # PhasmaPlayer.html/.js/.wasm. JSPI keeps startup's adapter and device waits synchronous (spec decision 4).
    set_target_properties(PhasmaPlayer PROPERTIES SUFFIX ".html")
    target_link_options(PhasmaPlayer PRIVATE
        -sJSPI
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=4MB
        -sEXIT_RUNTIME=0
    )

    # Engine runtime assets go into MEMFS at /RuntimeAssets, where Path looks under the web root (spec decision 13).
    # Emscripten writes them to PhasmaPlayer.data, which the page downloads before main.
    # ponytail: everything downloads before start; fetch on demand once the asset set grows.
    file(GLOB_RECURSE PE_WEB_RUNTIME_ASSETS CONFIGURE_DEPENDS "${PE_RUNTIME_ASSETS_SOURCE_DIR}/*")
    target_link_options(PhasmaPlayer PRIVATE "SHELL:--preload-file ${PE_RUNTIME_ASSETS_SOURCE_DIR}@/RuntimeAssets")
    set_property(TARGET PhasmaPlayer APPEND PROPERTY LINK_DEPENDS ${PE_WEB_RUNTIME_ASSETS})

    # The baked shader cache (tools/bake_web_shaders.py) goes into MEMFS at /ShaderCache as loose files, where the WebGPU
    # shader Impl reads _spv, _wgsl and the binding sidecars (spec decisions 7 and 13). Without it, the first shader is
    # an error naming its key. Reconfigure after the first bake so the folder is picked up.
    set(PE_WEB_SHADER_CACHE "${CMAKE_BINARY_DIR}/web-shader-cache/ShaderCache" CACHE PATH "Baked browser shader cache")
    if(EXISTS "${PE_WEB_SHADER_CACHE}/_wgsl")
        file(GLOB_RECURSE PE_WEB_SHADER_FILES CONFIGURE_DEPENDS "${PE_WEB_SHADER_CACHE}/*")
        target_link_options(PhasmaPlayer PRIVATE "SHELL:--preload-file ${PE_WEB_SHADER_CACHE}@/ShaderCache")
        set_property(TARGET PhasmaPlayer APPEND PROPERTY LINK_DEPENDS ${PE_WEB_SHADER_FILES})
    else()
        message(STATUS "[Web] no baked shader cache at ${PE_WEB_SHADER_CACHE}: run tools/bake_web_shaders.py")
    endif()

    # The project the browser player opens (spec decision 13): a folder with phasma_project.json, preloaded at /Project,
    # and a phasma_settings.json at the web root that points the player at its manifest. Bake the shaders with the same
    # project (tools/bake_web_shaders.py --project). Empty: the player starts with no project.
    # ponytail: loose files, not a game pack; everything downloads before start.
    set(PE_WEB_PROJECT "" CACHE PATH "Project folder (with phasma_project.json) the browser player opens")
    if(PE_WEB_PROJECT)
        if(NOT EXISTS "${PE_WEB_PROJECT}/phasma_project.json")
            message(FATAL_ERROR "PE_WEB_PROJECT has no phasma_project.json: ${PE_WEB_PROJECT}")
        endif()

        set(PE_WEB_SETTINGS "${CMAKE_BINARY_DIR}/web-settings/phasma_settings.json")
        file(WRITE "${PE_WEB_SETTINGS}" "{\"project_manifest\": \"/Project/phasma_project.json\"}\n")
        file(GLOB_RECURSE PE_WEB_PROJECT_FILES CONFIGURE_DEPENDS "${PE_WEB_PROJECT}/*")
        target_link_options(PhasmaPlayer PRIVATE
            "SHELL:--preload-file ${PE_WEB_PROJECT}@/Project"
            "SHELL:--preload-file ${PE_WEB_SETTINGS}@/phasma_settings.json"
        )
        set_property(TARGET PhasmaPlayer APPEND PROPERTY LINK_DEPENDS ${PE_WEB_PROJECT_FILES} ${PE_WEB_SETTINGS})
    endif()
else()
    target_link_libraries(PhasmaPlayer PRIVATE vulkan SDL2)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT PE_WEB)
    target_link_libraries(PhasmaPlayer PRIVATE SDL2main)
endif()

if(WIN32)
    target_link_libraries(PhasmaPlayer PRIVATE d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaPlayer PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(COMMAND pe_depend_on_root_runtime_dependencies)
    pe_depend_on_root_runtime_dependencies(PhasmaPlayer)
endif()
