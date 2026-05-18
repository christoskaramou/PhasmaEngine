cmake_minimum_required(VERSION 3.24)
project(PhasmaRuntimeUI CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaRuntimeUI = ${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT DEFINED PE_SHARED_THIRD_PARTY_DIR)
    set(PE_SHARED_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/third_party")
endif()

file(GLOB_RECURSE PHASMARUNTIMEUI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.h"
)

set(PHASMARUNTIMEUI_IMGUI_SOURCES
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_impl_sdl2.cpp"
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui/imgui_impl_vulkan.cpp"
)

if(WIN32)
    list(APPEND PHASMARUNTIMEUI_IMGUI_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/Code/GUI/Backends/imgui_impl_dx12.cpp"
    )
endif()

add_library(PhasmaRuntimeUI STATIC
    ${PHASMARUNTIMEUI_SOURCES}
    ${PHASMARUNTIMEUI_IMGUI_SOURCES}
)
set_target_properties(PhasmaRuntimeUI PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_precompile_headers(PhasmaRuntimeUI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/pch/PhasmaPch.h")

target_include_directories(PhasmaRuntimeUI PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Code"
)

target_include_directories(PhasmaRuntimeUI PRIVATE
    "${PE_SHARED_THIRD_PARTY_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaEditor/Code/GUI/Backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/vma"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PhasmaCore/third_party/vulkan"
)

target_link_libraries(PhasmaRuntimeUI PUBLIC PhasmaRuntime)
target_link_libraries(PhasmaRuntimeUI PRIVATE PhasmaCore vulkan)
if(DEFINED PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaRuntimeUI PRIVATE ${PE_CORE_LINK_DIRS})
endif()

if(WIN32)
    target_link_libraries(PhasmaRuntimeUI PRIVATE d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)
endif()
