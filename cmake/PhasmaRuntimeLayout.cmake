include_guard(GLOBAL)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/MinSizeRel/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo/Lib")

set(PHASMA_CONFIG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
set(PHASMA_WEBGPU_SAMPLE_OUTPUT_DIR "${PHASMA_CONFIG_OUTPUT_DIR}/Samples/WebGPU")
set(PHASMA_WEBGPU_SAMPLE_INSTALL_DIR "Samples/WebGPU")
set(PHASMA_RUNTIME_INSTALL_COMPONENT "Runtime")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PE_CORE_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/dxcompiler.dll"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/shaderc_shared.dll"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/SDL2.dll"
    )
    set(PE_MODEL_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/PhasmaCore/DLLs/assimp-vc143-mt.dll"
    )
else()
    set(PE_CORE_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/dx/libdxcompiler.so"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/libshaderc_shared.so"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/libshaderc_shared.so.1"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/vulkan/libvulkan.so"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/vulkan/libvulkan.so.1"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/vulkan/libvulkan.so.1.4.328"
    )
    set(PE_MODEL_RUNTIME_DEPENDENCY_FILES
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/assimp/libassimp.so"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/assimp/libassimp.so.6"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/assimp/libassimp.so.6.0.4"
    )
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
    add_custom_target(PhasmaRootRuntimeDependencies ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PHASMA_CONFIG_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PE_ROOT_RUNTIME_DEPENDENCY_FILES} "${PHASMA_CONFIG_OUTPUT_DIR}"
        DEPENDS ${PE_ROOT_RUNTIME_DEPENDENCY_FILES}
        COMMENT "[Phasma] Copying root runtime dependencies"
    )
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
    install(TARGETS PhasmaCore PhasmaLauncher
        RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        LIBRARY DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )
    install(TARGETS PhasmaEditor
        RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )
    install(TARGETS PhasmaEditorModule
        RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        LIBRARY DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )
    install(TARGETS PhasmaPlayer
        RUNTIME DESTINATION "." COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )

    pe_install_root_runtime_dependency_files(".")

    install(DIRECTORY "${PE_RUNTIME_ASSETS_SOURCE_DIR}/"
        DESTINATION "Assets"
        COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )

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
