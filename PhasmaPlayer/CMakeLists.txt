cmake_minimum_required(VERSION 3.22)
project(PhasmaPlayer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaPlayer = ${CMAKE_CURRENT_SOURCE_DIR}")

set(PHASMAPLAYER_RUNTIME_UI_SOURCES
    "${CMAKE_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends/ImGuiRuntimeUiBackend.cpp"
    "${CMAKE_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends/ImGuiRuntimeUiBackend.h"
    "${CMAKE_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends/ImGuiRuntimeUiStyle.h"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_impl_sdl2.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_impl_vulkan.cpp"
)

if(WIN32)
    list(APPEND PHASMAPLAYER_RUNTIME_UI_SOURCES
        "${CMAKE_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends/imgui_impl_dx12.cpp"
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

target_precompile_headers(PhasmaPlayer PRIVATE "${CMAKE_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")
target_include_directories(PhasmaPlayer PRIVATE
    "${CMAKE_SOURCE_DIR}/PhasmaRuntime/Code/UI/Backends"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui"
    "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party"
    "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/vma"
    "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/vulkan"
)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaPlayer PRIVATE ${PE_CORE_LINK_DIRS})
endif()
target_link_libraries(PhasmaPlayer PRIVATE PhasmaCore)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(PhasmaPlayer PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>")
elseif(MSVC)
    target_link_libraries(PhasmaPlayer PRIVATE PhasmaRuntime)
    target_link_options(PhasmaPlayer PRIVATE "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:PhasmaRuntime>")
else()
    target_link_libraries(PhasmaPlayer PRIVATE -Wl,--whole-archive PhasmaRuntime -Wl,--no-whole-archive)
endif()
target_link_libraries(PhasmaPlayer PRIVATE vulkan SDL2)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
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
