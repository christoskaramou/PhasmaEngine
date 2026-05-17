cmake_minimum_required(VERSION 3.10)
project(PhasmaPlayer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaPlayer = ${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(PhasmaPlayer "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

target_precompile_headers(PhasmaPlayer PRIVATE "${CMAKE_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")
target_link_directories(PhasmaPlayer PRIVATE ${PE_CORE_LINK_DIRS})
target_link_libraries(PhasmaPlayer PRIVATE PhasmaCore "$<LINK_LIBRARY:WHOLE_ARCHIVE,PhasmaRuntime>" SDL2 SDL2main)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PhasmaPlayer PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_command(TARGET PhasmaPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PhasmaCore>" "$<TARGET_FILE_DIR:PhasmaPlayer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/dxcompiler.dll" "$<TARGET_FILE_DIR:PhasmaPlayer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/shaderc_shared.dll" "$<TARGET_FILE_DIR:PhasmaPlayer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/SDL2.dll" "$<TARGET_FILE_DIR:PhasmaPlayer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/assimp-vc143-mt.dll" "$<TARGET_FILE_DIR:PhasmaPlayer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/libssl-3-x64.dll" "$<TARGET_FILE_DIR:PhasmaPlayer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/libcrypto-3-x64.dll" "$<TARGET_FILE_DIR:PhasmaPlayer>"
    )
else()
    add_custom_command(TARGET PhasmaPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PhasmaCore>" "$<TARGET_FILE_DIR:PhasmaPlayer>"
    )
endif()
