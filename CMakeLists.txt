cmake_minimum_required(VERSION 3.10)
project(PhasmaEditor C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "PhasmaEditor = ${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP${N}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP${N}")
    endif()
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

set(PE_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/Libs")
set(PE_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/third_party")

# --- Prebuilt Assimp (shared) ---
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

# --- Prebuilt MeshOptimizer (static) ---
message(STATUS "[Phasma] Using prebuilt MeshOptimizer ...")
add_library(meshoptimizer STATIC IMPORTED GLOBAL)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(meshoptimizer PROPERTIES
        IMPORTED_LOCATION "${PE_LIBS_DIR}/meshoptimizer/meshoptimizer.lib"
    )
else()
    set_target_properties(meshoptimizer PROPERTIES
        IMPORTED_LOCATION "${PE_LIBS_DIR}/meshoptimizer/libmeshoptimizer.a"
    )
endif()

# Add PhasmaCore subdirectory
add_subdirectory(PhasmaCore)

# Add PhasmaAgent subdirectory
add_subdirectory(PhasmaAgent)

# PhasmaEditor executable
file(GLOB_RECURSE CODE_SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code/*.cpp")
set(PE_EDITOR_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/third_party")

# --- Lua 5.4 (vendored, compiled from source) ---
file(GLOB LUA_SOURCES "${PE_EDITOR_THIRD_PARTY_DIR}/lua/*.c")
add_library(Lua STATIC ${LUA_SOURCES})
target_include_directories(Lua PUBLIC "${PE_EDITOR_THIRD_PARTY_DIR}/lua")
set_target_properties(Lua PROPERTIES LINKER_LANGUAGE C)
if(NOT MSVC)
    target_compile_options(Lua PRIVATE -w)
endif()
file(GLOB_RECURSE IMGUI_SRC_FILES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/imgui/*.cpp")
file(GLOB_RECURSE CACAO_SRC_FILES CONFIGURE_DEPENDS "${PE_EDITOR_THIRD_PARTY_DIR}/CACAO/*.cpp")
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets/*")
list(FILTER ASSET_FILES EXCLUDE REGEX "\\.obj$")
set_source_files_properties(${ASSET_FILES} PROPERTIES VS_TOOL_OVERRIDE "None")
add_executable(PhasmaEditor ${CODE_SRC_FILES} ${IMGUI_SRC_FILES} ${CACAO_SRC_FILES} ${ASSET_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/main.cpp)

# PhasmaEditor precompiled headers
target_precompile_headers(PhasmaEditor REUSE_FROM PhasmaCore)

# PhasmaEditor include directories
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor")
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Code")
target_include_directories(PhasmaEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaAgent/third_party")
target_include_directories(PhasmaEditor PRIVATE "${PE_EDITOR_THIRD_PARTY_DIR}")
target_include_directories(PhasmaEditor PRIVATE "${PE_EDITOR_THIRD_PARTY_DIR}/imgui")
target_include_directories(PhasmaEditor PRIVATE "${PE_EDITOR_THIRD_PARTY_DIR}/sol2")

# PhasmaEditor link directories
# Libraries
target_link_libraries(PhasmaEditor PRIVATE
    PhasmaCore
    PhasmaAgent
    assimp
    meshoptimizer
    Lua
)
target_compile_definitions(PhasmaEditor PRIVATE RAPIDJSON_HAS_STDSTRING=1 RAPIDJSON_NOMEMBERITERATORCLASS PE_SCRIPTS)

# target_compile_options(PhasmaEditor PRIVATE -fsanitize=address)
# target_link_options(PhasmaEditor PRIVATE -fsanitize=address)

# Copy assets to build directory (re-runs when assets change)
set(ASSETS_COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/AssetsCopy.stamp")
set(PHASMAEDITOR_RUNTIME_DIR "$<IF:$<CONFIG:Debug>,${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG},$<IF:$<CONFIG:Release>,${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE},$<IF:$<CONFIG:RelWithDebInfo>,${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO},${CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL}>>>")
add_custom_command(
    OUTPUT "${ASSETS_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaEditor/Assets" "${PHASMAEDITOR_RUNTIME_DIR}/Assets"
    COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_COPY_STAMP}"
    DEPENDS ${ASSET_FILES}
    COMMENT "[Phasma] Copying assets to build directory"
)
add_custom_target(PhasmaEditorAssets ALL DEPENDS "${ASSETS_COPY_STAMP}")
add_dependencies(PhasmaEditor PhasmaEditorAssets)

# Copy shader compilers for windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_command(TARGET PhasmaEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/dxcompiler.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/shaderc_shared.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/SDL2.dll" "$<TARGET_FILE_DIR:PhasmaEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PhasmaCore/DLLs/assimp-vc143-mt.dll" "$<TARGET_FILE_DIR:PhasmaEditor>")

    # Copy OpenSSL DLLs if available (OPENSSL_FOUND propagated from PhasmaAgent)
    if(OPENSSL_FOUND)
        if(NOT OPENSSL_ROOT_DIR AND DEFINED ENV{OPENSSL_ROOT_DIR})
            set(OPENSSL_ROOT_DIR "$ENV{OPENSSL_ROOT_DIR}")
        endif()
        if(OPENSSL_ROOT_DIR)
            file(GLOB _OPENSSL_DLLS "${OPENSSL_ROOT_DIR}/bin/libssl*.dll" "${OPENSSL_ROOT_DIR}/bin/libcrypto*.dll")
            foreach(_dll ${_OPENSSL_DLLS})
                add_custom_command(TARGET PhasmaEditor POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:PhasmaEditor>")
            endforeach()
        endif()
    endif()
endif()
