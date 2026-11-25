# The browser page of a PE_WEB executable: <target>.html/.js/.wasm/.data, its shell and the files it preloads into
# MEMFS before main (WebGPU RHI spec decisions 4 and 13). PhasmaPlayer and the web PhasmaEditor use it.
function(pe_web_page target shell)
    # <target>.html/.js/.wasm. JSPI keeps startup's adapter and device waits synchronous (spec decision 4).
    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    target_link_options(${target} PRIVATE
        -sJSPI
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=4MB
        -sEXIT_RUNTIME=0
    )

    # The game page: a full-window canvas sized by CSS, with a loading and error line.
    set(PE_WEB_SHELL "${shell}")
    target_link_options(${target} PRIVATE "SHELL:--shell-file ${PE_WEB_SHELL}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${PE_WEB_SHELL})

    # Engine runtime assets go into MEMFS at /RuntimeAssets, where Path looks under the web root (spec decision 13).
    # Emscripten writes them to <target>.data, which the page downloads before main.
    # ponytail: everything downloads before start; fetch on demand once the asset set grows.
    # A web export (PhasmaExport --target web) carries the engine assets in its pack, so it replaces them.
    set(PE_WEB_EXPORT "" CACHE PATH "PhasmaExport --target web folder the browser player runs at the web root")
    if(PE_WEB_EXPORT)
        file(GLOB_RECURSE PE_WEB_EXPORT_FILES CONFIGURE_DEPENDS "${PE_WEB_EXPORT}/*")
        target_link_options(${target} PRIVATE "SHELL:--preload-file ${PE_WEB_EXPORT}@/")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${PE_WEB_EXPORT_FILES})
    else()
        file(GLOB_RECURSE PE_WEB_RUNTIME_ASSETS CONFIGURE_DEPENDS "${PE_RUNTIME_ASSETS_SOURCE_DIR}/*")
        target_link_options(${target} PRIVATE "SHELL:--preload-file ${PE_RUNTIME_ASSETS_SOURCE_DIR}@/RuntimeAssets")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${PE_WEB_RUNTIME_ASSETS})
    endif()

    # The baked shader cache (tools/bake_web_shaders.py) goes into MEMFS at /ShaderCache as loose files, where the WebGPU
    # shader Impl reads _spv, _wgsl and the binding sidecars (spec decisions 7 and 13). Without it, the first shader is
    # an error naming its key. For the editor, validate the bake even when linking would otherwise be up to date.
    set(default_shader_cache "${CMAKE_BINARY_DIR}/web-shader-cache/ShaderCache")
    if(target STREQUAL "PhasmaEditor")
        set(default_shader_cache "${CMAKE_SOURCE_DIR}/build-wasm-editor/web-shader-cache/ShaderCache")
    endif()
    set(PE_WEB_SHADER_CACHE "${default_shader_cache}" CACHE PATH "Baked browser shader cache")
    set(PE_WEB_PROJECT "" CACHE PATH "Project folder (with phasma_project.json) the browser player opens")
    if(target STREQUAL "PhasmaEditor")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_custom_target(VerifyWebEditorBake
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/bake_web_shaders.py"
                    --verify-editor "${PE_WEB_SHADER_CACHE}" --project "${PE_WEB_PROJECT}"
            COMMENT "Checking the current editor shader bake and viewport modules"
            VERBATIM)
        add_dependencies(${target} VerifyWebEditorBake)
    endif()
    if(target STREQUAL "PhasmaEditor" OR EXISTS "${PE_WEB_SHADER_CACHE}/_wgsl")
        file(GLOB_RECURSE PE_WEB_SHADER_FILES CONFIGURE_DEPENDS "${PE_WEB_SHADER_CACHE}/*")
        target_link_options(${target} PRIVATE "SHELL:--preload-file ${PE_WEB_SHADER_CACHE}@/ShaderCache")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${PE_WEB_SHADER_FILES})
    else()
        message(STATUS "[Web] no baked shader cache at ${PE_WEB_SHADER_CACHE}: run tools/bake_web_shaders.py")
    endif()

    # The project the browser player opens (spec decision 13): a folder with phasma_project.json, preloaded at /Project,
    # and a phasma_settings.json at the web root that points the player at its manifest. Bake the shaders with the same
    # project (tools/bake_web_shaders.py --project). Empty: the player starts with no project.
    # ponytail: loose files, not a game pack; everything downloads before start.
    if(PE_WEB_PROJECT)
        if(NOT EXISTS "${PE_WEB_PROJECT}/phasma_project.json")
            message(FATAL_ERROR "PE_WEB_PROJECT has no phasma_project.json: ${PE_WEB_PROJECT}")
        endif()

        set(PE_WEB_SETTINGS "${CMAKE_BINARY_DIR}/web-settings/phasma_settings.json")
        file(WRITE "${PE_WEB_SETTINGS}" "{\"project_manifest\": \"/Project/phasma_project.json\"}\n")
        file(GLOB_RECURSE PE_WEB_PROJECT_FILES CONFIGURE_DEPENDS "${PE_WEB_PROJECT}/*")
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file ${PE_WEB_PROJECT}@/Project"
            "SHELL:--preload-file ${PE_WEB_SETTINGS}@/phasma_settings.json"
        )
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${PE_WEB_PROJECT_FILES} ${PE_WEB_SETTINGS})
    endif()
endfunction()
