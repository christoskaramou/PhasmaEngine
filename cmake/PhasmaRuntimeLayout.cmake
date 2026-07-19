include_guard(GLOBAL)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/Lib")

set(PHASMA_CONFIG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(PHASMA_WEBGPU_SAMPLE_OUTPUT_DIR "${PHASMA_CONFIG_OUTPUT_DIR}/Samples/WebGPU")
set(PHASMA_WEBGPU_SAMPLE_INSTALL_DIR "Samples/WebGPU")
set(PHASMA_RUNTIME_INSTALL_COMPONENT "Runtime")

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PE_CORE_RUNTIME_DEPENDENCY_FILES)
    set(PE_MODEL_RUNTIME_DEPENDENCY_FILES)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PE_CORE_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/Phasma/Core/DLLs/SDL2.dll"
    )
    if(PE_ENABLE_RUNTIME_SHADER_COMPILER)
        list(APPEND PE_CORE_RUNTIME_DEPENDENCY_FILES
            "${CMAKE_SOURCE_DIR}/Phasma/Core/DLLs/shaderc_shared.dll"
        )
    endif()
    list(APPEND PE_CORE_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/Phasma/Core/DLLs/dxcompiler.dll"
    )
    set(PE_MODEL_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/Phasma/Core/DLLs/assimp-vc143-mt.dll"
    )
else()
    set(PE_CORE_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/vulkan/libvulkan.so"
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/vulkan/libvulkan.so.1"
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/vulkan/libvulkan.so.1.4.350"
    )
    if(PE_ENABLE_RUNTIME_SHADER_COMPILER)
        list(APPEND PE_CORE_RUNTIME_DEPENDENCY_FILES
            "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/spirv/libshaderc_shared.so"
            "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/spirv/libshaderc_shared.so.1"
        )
    endif()
    list(APPEND PE_CORE_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/dx/libdxcompiler.so"
    )
    set(PE_MODEL_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/assimp/libassimp.so"
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/assimp/libassimp.so.6"
        "${CMAKE_SOURCE_DIR}/Phasma/Core/Libs/assimp/libassimp.so.6.0.4"
    )
endif()
if(NOT PE_ENABLE_ASSIMP)
    set(PE_MODEL_RUNTIME_DEPENDENCY_FILES)
endif()
set(PE_ROOT_RUNTIME_DEPENDENCY_FILES
    ${PE_CORE_RUNTIME_DEPENDENCY_FILES}
    ${PE_MODEL_RUNTIME_DEPENDENCY_FILES}
)

function(pe_set_target_output_subdir TARGET_NAME OUTPUT_SUBDIR)
    foreach(CONFIG_NAME DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
        set(RUNTIME_DIR_VAR "CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME}")
        set(LIBRARY_DIR_VAR "CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME}")
        set(ARCHIVE_DIR_VAR "CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_NAME}")

        set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME} "${${RUNTIME_DIR_VAR}}/${OUTPUT_SUBDIR}"
            LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME} "${${LIBRARY_DIR_VAR}}/${OUTPUT_SUBDIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_NAME} "${${ARCHIVE_DIR_VAR}}"
        )
    endforeach()
endfunction()

function(pe_add_root_runtime_dependencies_target)
    set(PHASMA_ROOT_RUNTIME_DEPENDENCIES_STAMP "${CMAKE_BINARY_DIR}/CMakeFiles/PhasmaRootRuntimeDependencies-$<CONFIG>.stamp")
    if(PE_ROOT_RUNTIME_DEPENDENCY_FILES)
        add_custom_command(
            OUTPUT "${PHASMA_ROOT_RUNTIME_DEPENDENCIES_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/CMakeFiles" "${PHASMA_CONFIG_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PE_ROOT_RUNTIME_DEPENDENCY_FILES} "${PHASMA_CONFIG_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch "${PHASMA_ROOT_RUNTIME_DEPENDENCIES_STAMP}"
            DEPENDS ${PE_ROOT_RUNTIME_DEPENDENCY_FILES}
            COMMENT "[Phasma] Copying root runtime dependencies"
        )
    else()
        add_custom_command(
            OUTPUT "${PHASMA_ROOT_RUNTIME_DEPENDENCIES_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/CMakeFiles" "${PHASMA_CONFIG_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch "${PHASMA_ROOT_RUNTIME_DEPENDENCIES_STAMP}"
            COMMENT "[Phasma] No root runtime dependencies to copy"
        )
    endif()
    add_custom_target(PhasmaRootRuntimeDependencies ALL DEPENDS "${PHASMA_ROOT_RUNTIME_DEPENDENCIES_STAMP}")
endfunction()

function(pe_depend_on_root_runtime_dependencies TARGET_NAME)
    if(TARGET PhasmaRootRuntimeDependencies)
        add_dependencies(${TARGET_NAME} PhasmaRootRuntimeDependencies)
    endif()
endfunction()

function(pe_install_dependency_files INSTALL_DESTINATION)
    if(ARGN)
        install(FILES ${ARGN}
            DESTINATION "${INSTALL_DESTINATION}"
            COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()
endfunction()

function(pe_install_root_runtime_dependency_files INSTALL_DESTINATION)
    pe_install_dependency_files("${INSTALL_DESTINATION}" ${PE_ROOT_RUNTIME_DEPENDENCY_FILES})
endfunction()

function(pe_install_runtime_layout)
    set(PHASMA_INSTALL_SHARED_TARGETS PhasmaCore)
    if(TARGET PhasmaLauncher)
        list(APPEND PHASMA_INSTALL_SHARED_TARGETS PhasmaLauncher)
    endif()
    install(TARGETS ${PHASMA_INSTALL_SHARED_TARGETS}
        RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        LIBRARY DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )
    if(TARGET PhasmaEditor)
        install(TARGETS PhasmaEditor
            RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()
    if(TARGET PhasmaEditorModule)
        install(TARGETS PhasmaEditorModule
            RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
            LIBRARY DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()
    if(TARGET PhasmaPlayer)
        install(TARGETS PhasmaPlayer
            RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()
    if(TARGET PhasmaExport)
        install(TARGETS PhasmaExport
            RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()
    if(TARGET PhasmaProfiler)
        install(TARGETS PhasmaProfiler
            RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()
    # The mesh-cook tool ships beside the editor so File > Import (which shells out to it) works in
    # installed layouts too, not only the build tree. It is the only target that links Assimp.
    if(TARGET PhasmaCook)
        install(TARGETS PhasmaCook
            RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()

    pe_install_root_runtime_dependency_files(".")

    install(DIRECTORY "${PE_RUNTIME_ASSETS_SOURCE_DIR}/"
        DESTINATION "Assets"
        COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )

    if(TARGET PhasmaEditor AND DEFINED PE_EDITOR_ASSETS_SOURCE_DIR AND EXISTS "${PE_EDITOR_ASSETS_SOURCE_DIR}")
        install(DIRECTORY "${PE_EDITOR_ASSETS_SOURCE_DIR}/"
            DESTINATION "EditorAssets"
            COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )
    endif()

    pe_install_default_runtime_settings()
endfunction()

function(pe_install_default_runtime_settings)
    install(CODE [==[
if(NOT EXISTS "${CMAKE_INSTALL_PREFIX}/phasma_settings.json")
    file(WRITE "${CMAKE_INSTALL_PREFIX}/phasma_settings.json" [=[{
  "graphics_api": "vulkan"
}
]=])
endif()
]==]
        COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )
endfunction()
