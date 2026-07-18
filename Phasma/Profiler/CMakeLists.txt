cmake_minimum_required(VERSION 3.22)
project(PhasmaProfiler CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaProfiler = ${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(PhasmaProfiler "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

target_precompile_headers(PhasmaProfiler PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_include_directories(PhasmaProfiler PRIVATE
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui"
    "${CMAKE_SOURCE_DIR}/Phasma/Core/third_party/rapidjson/include"
)
if(PE_CORE_LINK_DIRS)
    target_link_directories(PhasmaProfiler PRIVATE ${PE_CORE_LINK_DIRS})
endif()
target_link_libraries(PhasmaProfiler PRIVATE PhasmaCore SDL2 SDL2main)
target_sources(PhasmaProfiler PRIVATE
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/imgui_impl_sdl2.cpp"
)

if(WIN32)
    target_link_libraries(PhasmaProfiler PRIVATE ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaProfiler PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(COMMAND pe_depend_on_root_runtime_dependencies)
    pe_depend_on_root_runtime_dependencies(PhasmaProfiler)
endif()
