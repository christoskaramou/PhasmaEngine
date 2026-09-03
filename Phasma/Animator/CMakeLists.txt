# PhasmaAnimator: the standalone animation program. Its own host over PhasmaRuntime's scene renderer, the
# Animation Timeline / Rig Editor / pose tools (this tree), and the editor's shared ImGui plumbing
# (GUIBackend, GUIState, Helpers, SelectionManager) - never the editor GUI itself.
file(GLOB_RECURSE PHASMA_ANIMATOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp")
file(GLOB PHASMA_ANIMATOR_IMGUI_SOURCES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/*.cpp")
set(PHASMA_ANIMATOR_SHARED_EDITOR_SOURCES
    "${CMAKE_SOURCE_DIR}/Phasma/Editor/Code/GUI/Backends/GUIBackend.cpp"
    "${CMAKE_SOURCE_DIR}/Phasma/Editor/Code/GUI/GUIState.cpp"
    "${CMAKE_SOURCE_DIR}/Phasma/Editor/Code/Scene/SelectionManager.cpp"
)
if(WIN32)
    list(APPEND PHASMA_ANIMATOR_SHARED_EDITOR_SOURCES
        "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends/imgui_impl_dx12.cpp"
    )
endif()

add_executable(PhasmaAnimator
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    ${PHASMA_ANIMATOR_SOURCES}
    ${PHASMA_ANIMATOR_IMGUI_SOURCES}
    ${PHASMA_ANIMATOR_SHARED_EDITOR_SOURCES}
)
target_precompile_headers(PhasmaAnimator PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_include_directories(PhasmaAnimator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Code"
    "${CMAKE_SOURCE_DIR}/Phasma/Editor/Code"
    "${CMAKE_SOURCE_DIR}/Phasma/Runtime/Code/UI/Backends"
    "${CMAKE_SOURCE_DIR}/Phasma/MCP/third_party"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui"
    "${PE_SHARED_THIRD_PARTY_DIR}"
    "${PE_THIRD_PARTY_DIR}"
    "${PE_THIRD_PARTY_DIR}/stb"
    "${PE_THIRD_PARTY_DIR}/vma"
    "${PE_THIRD_PARTY_DIR}/vulkan"
)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaAnimator PRIVATE ${PE_CORE_LINK_DIRS})
endif()
target_link_libraries(PhasmaAnimator PRIVATE PhasmaCore)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(PhasmaAnimator PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>")
elseif(MSVC)
    target_link_libraries(PhasmaAnimator PRIVATE PhasmaRuntime)
    target_link_options(PhasmaAnimator PRIVATE "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:PhasmaRuntime>")
else()
    target_link_libraries(PhasmaAnimator PRIVATE -Wl,--whole-archive PhasmaRuntime -Wl,--no-whole-archive)
endif()
target_link_libraries(PhasmaAnimator PRIVATE vulkan SDL2 SDL2main)
if(WIN32)
    target_link_libraries(PhasmaAnimator PRIVATE d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib comdlg32.lib)
endif()
# PhasmaRuntime keeps these private; a host that creates the systems needs the same switches.
if(PE_PHYSICS AND TARGET Jolt)
    target_compile_definitions(PhasmaAnimator PRIVATE PE_PHYSICS)
endif()
if(PE_PHYSICS2D AND TARGET box2d)
    target_compile_definitions(PhasmaAnimator PRIVATE PE_PHYSICS2D)
endif()
if(PE_AUDIO)
    target_include_directories(PhasmaAnimator PRIVATE "${PE_SHARED_THIRD_PARTY_DIR}/miniaudio")
    target_compile_definitions(PhasmaAnimator PRIVATE PE_AUDIO)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaAnimator PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()
if(COMMAND pe_depend_on_root_runtime_dependencies)
    pe_depend_on_root_runtime_dependencies(PhasmaAnimator)
endif()
