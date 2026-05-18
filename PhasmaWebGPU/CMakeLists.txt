cmake_minimum_required(VERSION 3.24)
project(PhasmaWebGPU CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "PhasmaWebGPU = ${CMAKE_CURRENT_SOURCE_DIR}")

include(FetchContent)
FetchContent_Declare(
    webgpu_headers
    GIT_REPOSITORY https://github.com/webgpu-native/webgpu-headers.git
    GIT_TAG main
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(webgpu_headers)

# The webgpu-native headers repo ships webgpu.h at the repo root (no webgpu/
# subdir). Create a wrapper directory so code can include <webgpu/webgpu.h>.
set(WEBGPU_INCLUDE_WRAP "${CMAKE_BINARY_DIR}/webgpu_include")
file(MAKE_DIRECTORY "${WEBGPU_INCLUDE_WRAP}/webgpu")
configure_file(
    "${webgpu_headers_SOURCE_DIR}/webgpu.h"
    "${WEBGPU_INCLUDE_WRAP}/webgpu/webgpu.h"
    COPYONLY
)

file(GLOB_RECURSE PWGPU_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Code/*.cpp"
)

option(PE_WEBGPU_SHARED "Build PhasmaWebGPU as a shared library" OFF)
option(PE_WEBGPU_WGSL "Build in-engine WGSL to SPIR-V via naga (Rust)" ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PWGPU_SPIRV_CROSS_CORE_LIB
        "$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/spirv-cross-cored.lib>"
        "$<$<CONFIG:Release>:${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/spirv-cross-core.lib>"
        "$<$<CONFIG:MinSizeRel>:${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/spirv-cross-core.lib>"
        "$<$<CONFIG:RelWithDebInfo>:${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/spirv-cross-core.lib>"
    )
    set(PWGPU_DXCOMPILER_LIB "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/dx/dxcompiler.lib")
    set(PWGPU_SDL_LIBS
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/SDL2/SDL2.lib"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/SDL2/SDL2main.lib"
    )
    set(PWGPU_VULKAN_LIB "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/vulkan/vulkan.lib")
else()
    set(PWGPU_SPIRV_CROSS_CORE_LIB "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/spirv/libspirv-cross-core.a")
    set(PWGPU_DXCOMPILER_LIB "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/dx/libdxcompiler.so")
    set(PWGPU_SDL_LIBS
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/SDL2/libSDL2.a"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/SDL2/libSDL2main.a"
    )
    set(PWGPU_VULKAN_LIB "${CMAKE_SOURCE_DIR}/PhasmaCore/Libs/vulkan/libvulkan.so")
endif()

if (PE_WEBGPU_SHARED)
    add_library(PhasmaWebGPU SHARED ${PWGPU_SOURCES})
    target_compile_definitions(
        PhasmaWebGPU
        PRIVATE PWGPU_BUILDING PWGPU_SHARED_LIBRARY WGPU_SHARED_LIBRARY WGPU_IMPLEMENTATION
        PUBLIC PWGPU_SHARED_LIBRARY
    )
else()
    add_library(PhasmaWebGPU STATIC ${PWGPU_SOURCES})
endif()

set_target_properties(PhasmaWebGPU PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_precompile_headers(PhasmaWebGPU PRIVATE "${CMAKE_SOURCE_DIR}/PhasmaCore/pch/PhasmaPch.h")
target_include_directories(
    PhasmaWebGPU
    PUBLIC "${WEBGPU_INCLUDE_WRAP}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Code"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/dxc"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/spirv_cross"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/vma"
        "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/vulkan"
)
target_link_libraries(PhasmaWebGPU PUBLIC PhasmaCore)
target_link_libraries(PhasmaWebGPU PRIVATE
    ${PWGPU_SPIRV_CROSS_CORE_LIB}
    ${PWGPU_DXCOMPILER_LIB}
    ${PWGPU_SDL_LIBS}
    ${PWGPU_VULKAN_LIB}
)

if (PE_WEBGPU_WGSL)
    FetchContent_Declare(
        Corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        GIT_TAG v0.5
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(Corrosion)

    corrosion_import_crate(
        MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/WgslBridge/Cargo.toml"
        PROFILE release
        CRATE_TYPES staticlib
    )

    target_link_libraries(PhasmaWebGPU PRIVATE naga_bridge)
    target_include_directories(
        PhasmaWebGPU
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/WgslBridge/include"
    )
    target_compile_definitions(PhasmaWebGPU PRIVATE PE_WEBGPU_WGSL=1)
endif()

if (MSVC)
    target_compile_options(PhasmaWebGPU PRIVATE /W3)
else()
    target_compile_options(PhasmaWebGPU PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(NOT DEFINED PHASMA_WEBGPU_SAMPLE_OUTPUT_DIR)
    set(PHASMA_WEBGPU_SAMPLE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>/Samples/WebGPU")
endif()
if(NOT DEFINED PHASMA_WEBGPU_SAMPLE_INSTALL_DIR)
    set(PHASMA_WEBGPU_SAMPLE_INSTALL_DIR "Samples/WebGPU")
endif()

function(pe_configure_webgpu_executable TARGET_NAME)
    if(COMMAND pe_set_target_output_subdir)
        pe_set_target_output_subdir(${TARGET_NAME} "Samples/WebGPU")
    else()
        set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PHASMA_WEBGPU_SAMPLE_OUTPUT_DIR}"
        )
    endif()

    if(COMMAND pe_depend_on_root_runtime_dependencies)
        pe_depend_on_root_runtime_dependencies(${TARGET_NAME})
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(${TARGET_NAME} PROPERTIES
            BUILD_RPATH "$ORIGIN;$ORIGIN/../.."
            INSTALL_RPATH "$ORIGIN;$ORIGIN/../.."
        )
    endif()

    install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION "${PHASMA_WEBGPU_SAMPLE_INSTALL_DIR}"
        COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
    )
endfunction()

option(PE_WEBGPU_TESTS "Build PhasmaWebGPU smoke tests" ON)
if (PE_WEBGPU_TESTS)
    add_executable(WebGPUSmokeTest "${CMAKE_CURRENT_SOURCE_DIR}/Tests/SmokeTest.cpp")
    target_link_libraries(WebGPUSmokeTest PRIVATE PhasmaWebGPU)
    target_precompile_headers(WebGPUSmokeTest REUSE_FROM PhasmaWebGPU)
    target_include_directories(WebGPUSmokeTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Code")
    pe_configure_webgpu_executable(WebGPUSmokeTest)

    add_executable(WebGPURenderTest "${CMAKE_CURRENT_SOURCE_DIR}/Tests/RenderTest.cpp")
    target_link_libraries(WebGPURenderTest PRIVATE PhasmaWebGPU)
    target_precompile_headers(WebGPURenderTest REUSE_FROM PhasmaWebGPU)
    target_include_directories(WebGPURenderTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Code")
    pe_configure_webgpu_executable(WebGPURenderTest)

    add_executable(WebGPUWgslSmokeTest "${CMAKE_CURRENT_SOURCE_DIR}/Tests/WgslSmokeTest.cpp")
    target_link_libraries(WebGPUWgslSmokeTest PRIVATE PhasmaWebGPU)
    target_precompile_headers(WebGPUWgslSmokeTest REUSE_FROM PhasmaWebGPU)
    target_include_directories(WebGPUWgslSmokeTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Code")
    if (PE_WEBGPU_WGSL)
        target_compile_definitions(WebGPUWgslSmokeTest PRIVATE PE_WEBGPU_WGSL=1)
    endif()
    pe_configure_webgpu_executable(WebGPUWgslSmokeTest)

    if (WIN32)
        add_executable(WebGPUDxilCacheHashTest "${CMAKE_CURRENT_SOURCE_DIR}/Tests/DxilCacheHashTest.cpp")
        target_link_libraries(WebGPUDxilCacheHashTest PRIVATE PhasmaWebGPU)
        target_precompile_headers(WebGPUDxilCacheHashTest REUSE_FROM PhasmaWebGPU)
        target_include_directories(WebGPUDxilCacheHashTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Code")
        pe_configure_webgpu_executable(WebGPUDxilCacheHashTest)
    endif()

    # pe_add_webgpu_sample(
    #   TARGET <exe name, must start with WebGPU>
    #   DIR <subfolder under Tests/>
    #   SOURCE <cpp path, absolute or relative to Tests/<DIR>>
    #   SHADERS <basenames without .hlsl, copied next to the exe>
    #   ASSETS <list of "src|dstname" pairs copied next to the exe>
    # )
    function(pe_add_webgpu_sample)
        cmake_parse_arguments(
            S
            "USE_COMMON_APP"
            "TARGET;DIR;SOURCE"
            "SHADERS;ASSETS"
            ${ARGN}
        )

        foreach (A IN LISTS S_ASSETS)
            string(REPLACE "|" ";" PARTS "${A}")
            list(GET PARTS 0 ASRC)
            if (NOT EXISTS "${ASRC}")
                message(STATUS "Skipping ${S_TARGET}: missing asset ${ASRC}")
                return()
            endif()
        endforeach()

        string(REGEX REPLACE "^WebGPU" "" SHADER_FOLDER "${S_TARGET}")
        set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/Tests/${S_DIR}/Shaders")

        if (IS_ABSOLUTE "${S_SOURCE}")
            set(SRC_PATH "${S_SOURCE}")
        else()
            set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Tests/${S_DIR}/${S_SOURCE}")
        endif()

        add_executable(${S_TARGET} "${SRC_PATH}")
        target_link_libraries(${S_TARGET} PRIVATE PhasmaWebGPU)
        if (S_USE_COMMON_APP)
            target_link_libraries(${S_TARGET} PRIVATE WebGPUSampleCommon)
        endif()
        target_precompile_headers(${S_TARGET} REUSE_FROM PhasmaWebGPU)
        target_include_directories(
            ${S_TARGET}
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/Code"
                "${CMAKE_SOURCE_DIR}/PhasmaCore"
                "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party"
                "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/dxc"
                "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/glm"
                "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/rapidjson/include"
                "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/vma"
                "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/vulkan"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests"
        )
        pe_configure_webgpu_executable(${S_TARGET})

        file(GLOB_RECURSE SHADER_SOURCE_FILES CONFIGURE_DEPENDS "${SHADER_SRC}/*")
        add_custom_command(
            TARGET ${S_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${S_TARGET}>/Shaders/${SHADER_FOLDER}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SHADER_SRC}"
                "$<TARGET_FILE_DIR:${S_TARGET}>/Shaders/${SHADER_FOLDER}"
            COMMENT "Copying ${SHADER_FOLDER} shader sources next to ${S_TARGET}"
        )
        install(DIRECTORY "${SHADER_SRC}/"
            DESTINATION "${PHASMA_WEBGPU_SAMPLE_INSTALL_DIR}/Shaders/${SHADER_FOLDER}"
            COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
        )

        foreach (A IN LISTS S_ASSETS)
            string(REPLACE "|" ";" PARTS "${A}")
            list(GET PARTS 0 ASRC)
            list(GET PARTS 1 ADST)
            get_filename_component(ADST_DIR "${ADST}" DIRECTORY)
            if (ADST_DIR)
                add_custom_command(
                    TARGET ${S_TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_FILE_DIR:${S_TARGET}>/${ADST_DIR}"
                )
            endif()

            add_custom_command(
                TARGET ${S_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ASRC}"
                    "$<TARGET_FILE_DIR:${S_TARGET}>/${ADST}"
                COMMENT "Copying asset ${ADST} for ${S_TARGET}"
            )

            get_filename_component(ADST_NAME "${ADST}" NAME)
            if (ADST_DIR)
                install(FILES "${ASRC}"
                    DESTINATION "${PHASMA_WEBGPU_SAMPLE_INSTALL_DIR}/${ADST_DIR}"
                    RENAME "${ADST_NAME}"
                    COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
                )
            else()
                install(FILES "${ASRC}"
                    DESTINATION "${PHASMA_WEBGPU_SAMPLE_INSTALL_DIR}"
                    RENAME "${ADST_NAME}"
                    COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"
                )
            endif()
        endforeach()
    endfunction()

    set(WEBGPU_SAMPLE_COMMON_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Common/SampleApp.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Common/SampleBase.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Common/SampleUtils.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Common/FrameCapture.cpp"
    )

    add_library(WebGPUSampleCommon STATIC ${WEBGPU_SAMPLE_COMMON_SOURCES})
    target_link_libraries(WebGPUSampleCommon PRIVATE PhasmaWebGPU)
    target_precompile_headers(WebGPUSampleCommon REUSE_FROM PhasmaWebGPU)
    target_include_directories(
        WebGPUSampleCommon
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/Code"
            "${CMAKE_CURRENT_SOURCE_DIR}/Tests"
            "${CMAKE_SOURCE_DIR}/PhasmaCore"
            "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/dxc"
            "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/glm"
            "${CMAKE_SOURCE_DIR}/PhasmaCore/third_party/rapidjson/include"
    )

    set(WEBGPU_SAMPLES_ASSETS "${CMAKE_SOURCE_DIR}/../webgpu-samples/public/assets/img")

    pe_add_webgpu_sample(
        TARGET WebGPUDeferredTest
        DIR DeferredRendering
        SOURCE DeferredRenderingTest.cpp
        USE_COMMON_APP
        SHADERS gbuffer.vert gbuffer.pixel quad.vert deferred.pixel lights.comp
        ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/DeferredRendering/Assets/stanfordDragon.bin|Assets/stanfordDragon.bin"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUParticlesTest
        DIR Particles
        SOURCE ParticlesTest.cpp
        USE_COMMON_APP
        SHADERS particles.comp particles.vert particles.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/webgpu.png|webgpu.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPURotatingCubeTest
        DIR RotatingCube
        SOURCE RotatingCubeTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUTexturedCubeTest
        DIR TexturedCube
        SOURCE TexturedCubeTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/Di-3d.png|Di-3d.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUInstancedCubeTest
        DIR InstancedCube
        SOURCE InstancedCubeTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUTwoCubesTest
        DIR TwoCubes
        SOURCE TwoCubesTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUFractalCubeTest
        DIR FractalCube
        SOURCE FractalCubeTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUGameOfLifeTest
        DIR GameOfLife
        SOURCE GameOfLifeTest.cpp
        USE_COMMON_APP
        SHADERS life.comp life.vert life.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUCamerasTest
        DIR Cameras
        SOURCE CamerasTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/Di-3d.png|Di-3d.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUShadowMappingTest
        DIR ShadowMapping
        SOURCE ShadowMappingTest.cpp
        USE_COMMON_APP
        SHADERS shadow.vert lit.vert lit.pixel
        ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/ShadowMapping/Assets/stanfordDragon.bin|stanfordDragon.bin"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUHelloTriangleTest
        DIR HelloTriangle
        SOURCE HelloTriangleTest.cpp
        USE_COMMON_APP
        SHADERS triangle.wgsl
    )

    pe_add_webgpu_sample(
        TARGET WebGPUHelloTriangleMSAATest
        DIR HelloTriangleMSAA
        SOURCE HelloTriangleMSAATest.cpp
        USE_COMMON_APP
        SHADERS triangle.vert triangle.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUCubemapTest
        DIR Cubemap
        SOURCE CubemapTest.cpp
        USE_COMMON_APP
        SHADERS cubemap.vert cubemap.pixel
        ASSETS
            "${WEBGPU_SAMPLES_ASSETS}/cubemap/posx.jpg|posx.jpg"
            "${WEBGPU_SAMPLES_ASSETS}/cubemap/negx.jpg|negx.jpg"
            "${WEBGPU_SAMPLES_ASSETS}/cubemap/posy.jpg|posy.jpg"
            "${WEBGPU_SAMPLES_ASSETS}/cubemap/negy.jpg|negy.jpg"
            "${WEBGPU_SAMPLES_ASSETS}/cubemap/posz.jpg|posz.jpg"
            "${WEBGPU_SAMPLES_ASSETS}/cubemap/negz.jpg|negz.jpg"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUAnimometerTest
        DIR Animometer
        SOURCE AnimometerTest.cpp
        USE_COMMON_APP
        SHADERS animometer.vert animometer.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUImageBlurTest
        DIR ImageBlur
        SOURCE ImageBlurTest.cpp
        USE_COMMON_APP
        SHADERS blur.comp quad.vert quad.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/Di-3d.png|Di-3d.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUComputeBoidsTest
        DIR ComputeBoids
        SOURCE ComputeBoidsTest.cpp
        USE_COMMON_APP
        SHADERS updateSprites.comp sprite.vert sprite.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUResizeCanvasTest
        DIR ResizeCanvas
        SOURCE ResizeCanvasTest.cpp
        USE_COMMON_APP
        SHADERS triangle.vert triangle.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUOcclusionQueryTest
        DIR OcclusionQuery
        SOURCE OcclusionQueryTest.cpp
        USE_COMMON_APP
        SHADERS solidColorLit.vert solidColorLit.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPURenderBundlesTest
        DIR RenderBundles
        SOURCE RenderBundlesTest.cpp
        USE_COMMON_APP
        SHADERS mesh.vert mesh.pixel
        ASSETS
            "${WEBGPU_SAMPLES_ASSETS}/saturn.jpg|saturn.jpg"
            "${WEBGPU_SAMPLES_ASSETS}/moon.jpg|moon.jpg"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUBitonicSortTest
        DIR BitonicSort
        SOURCE BitonicSortTest.cpp
        USE_COMMON_APP
        SHADERS bitonic.comp display.vert display.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUABufferTest
        DIR ABuffer
        SOURCE ABufferTest.cpp
        USE_COMMON_APP
        SHADERS opaque.vert opaque.pixel translucent.vert translucent.pixel composite.vert composite.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUReversedZTest
        DIR ReversedZ
        SOURCE ReversedZTest.cpp
        USE_COMMON_APP
        SHADERS
            vertex.vert fragment.pixel
            vertexDepthPrePass.vert
            vertexPrecisionErrorPass.vert fragmentPrecisionErrorPass.pixel
            vertexTextureQuad.vert fragmentTextureQuad.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUBlendingTest
        DIR Blending
        SOURCE BlendingTest.cpp
        USE_COMMON_APP
        SHADERS texturedQuad.vert texturedQuad.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUSamplerParametersTest
        DIR SamplerParameters
        SOURCE SamplerParametersTest.cpp
        USE_COMMON_APP
        SHADERS texturedSquare.vert texturedSquare.pixel showTexture.vert showTexture.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUGenerateMipmapTest
        DIR GenerateMipmap
        SOURCE GenerateMipmapTest.cpp
        USE_COMMON_APP
        SHADERS
            generateMipmap.vert
            generateMipmap2d.pixel
            generateMipmap2dArray.pixel
            generateMipmapCube.pixel
            generateMipmapCubeArray.pixel
            texturedGeometry2d.vert texturedGeometry2d.pixel
            texturedGeometry2dArray.vert texturedGeometry2dArray.pixel
            texturedGeometryCube.vert texturedGeometryCube.pixel
            texturedGeometryCubeArray.vert texturedGeometryCubeArray.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUStencilMaskTest
        DIR StencilMask
        SOURCE StencilMaskTest.cpp
        USE_COMMON_APP
        SHADERS simpleLighting.vert simpleLighting.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUWireframeTest
        DIR Wireframe
        SOURCE WireframeTest.cpp
        USE_COMMON_APP
        SHADERS solidColorLit.vert solidColorLit.pixel wireframeLine.vert wireframeLine.pixel wireframeBary.vert wireframeBary.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUNormalMapTest
        DIR NormalMap
        SOURCE NormalMapTest.cpp
        USE_COMMON_APP
        SHADERS normalMap.vert normalMap.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/wood_albedo.png|wood_albedo.png"
               "${WEBGPU_SAMPLES_ASSETS}/spiral_normal.png|spiral_normal.png"
               "${WEBGPU_SAMPLES_ASSETS}/spiral_height.png|spiral_height.png"
               "${WEBGPU_SAMPLES_ASSETS}/toybox_normal.png|toybox_normal.png"
               "${WEBGPU_SAMPLES_ASSETS}/toybox_height.png|toybox_height.png"
               "${WEBGPU_SAMPLES_ASSETS}/brickwall_albedo.png|brickwall_albedo.png"
               "${WEBGPU_SAMPLES_ASSETS}/brickwall_normal.png|brickwall_normal.png"
               "${WEBGPU_SAMPLES_ASSETS}/brickwall_height.png|brickwall_height.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUPointsTest
        DIR Points
        SOURCE PointsTest.cpp
        USE_COMMON_APP
        SHADERS distance-sized-points.vert fixed-size-points.vert orange.pixel textured.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUPrimitivePickingTest
        DIR PrimitivePicking
        SOURCE PrimitivePickingTest.cpp
        USE_COMMON_APP
        SHADERS
            vertexForwardRendering.vert
            fragmentForwardRendering.pixel
            vertexTextureQuad.vert
            fragmentPrimitivesDebugView.pixel
            computePickPrimitive.comp
    )

    pe_add_webgpu_sample(
        TARGET WebGPUTransparentCanvasTest
        DIR TransparentCanvas
        SOURCE TransparentCanvasTest.cpp
        USE_COMMON_APP
        SHADERS cube.vert cube.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUTimestampQueryTest
        DIR TimestampQuery
        SOURCE TimestampQueryTest.cpp
        USE_COMMON_APP
        SHADERS timestamp.vert timestamp.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUVolumeRenderingTexture3DTest
        DIR VolumeRenderingTexture3D
        SOURCE VolumeRenderingTexture3DTest.cpp
        USE_COMMON_APP
        SHADERS volume.vert volume.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/volume/t1_icbm_normal_1mm_pn0_rf0_180x216x180_uint8_1x1.bin-gz|volume/t1_icbm_normal_1mm_pn0_rf0_180x216x180_uint8_1x1.bin-gz"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUTextRenderingMsdfTest
        DIR TextRenderingMsdf
        SOURCE TextRenderingMsdfTest.cpp
        USE_COMMON_APP
        SHADERS msdfText.vert msdfText.pixel cube.vert cube.pixel
        ASSETS "${CMAKE_SOURCE_DIR}/../webgpu-samples/public/assets/font/ya-hei-ascii-msdf.json|font/ya-hei-ascii-msdf.json"
               "${CMAKE_SOURCE_DIR}/../webgpu-samples/public/assets/font/ya-hei-ascii.png|font/ya-hei-ascii.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUCornellTest
        DIR Cornell
        SOURCE CornellTest.cpp
        USE_COMMON_APP
        SHADERS radiosity.comp accumulationToLightmap.comp rasterizer.vert rasterizer.pixel raytracer.comp tonemapper.vert tonemapper.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUSkinnedMeshTest
        DIR SkinnedMesh
        SOURCE SkinnedMeshTest.cpp
        USE_COMMON_APP
        SHADERS gltf.vert gltf.pixel grid.vert grid.pixel
        ASSETS "${CMAKE_SOURCE_DIR}/../webgpu-samples/public/assets/gltf/whale.glb|gltf/whale.glb"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUPristineGridTest
        DIR PristineGrid
        SOURCE PristineGridTest.cpp
        USE_COMMON_APP
        SHADERS pristineGrid.vert pristineGrid.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUAlphaToCoverageEmulatorTest
        DIR AlphaToCoverageEmulator
        SOURCE AlphaToCoverageEmulatorTest.cpp
        USE_COMMON_APP
        SHADERS
            foliage.vert foliage.pixel
            crossingGradients.vert crossingGradients.pixel
            showMultisampleTexture.vert showMultisampleTexture.pixel
    )

    pe_add_webgpu_sample(
        TARGET WebGPUBundleCullingTest
        DIR BundleCulling
        SOURCE BundleCullingTest.cpp
        USE_COMMON_APP
        SHADERS geometry.vert geometryCulled.vert geometry.pixel cull.comp
    )

    pe_add_webgpu_sample(
        TARGET WebGPUParticleLifeTest
        DIR ParticleLife
        SOURCE ParticleLifeTest.cpp
        USE_COMMON_APP
        SHADERS render.vert render.pixel cell.comp prefix.comp sort.comp sim.comp
    )

    pe_add_webgpu_sample(
        TARGET WebGPUWorkloadSimulatorTest
        DIR WorkloadSimulator
        SOURCE WorkloadSimulatorTest.cpp
        USE_COMMON_APP
        SHADERS workload.vert workload.pixel
        ASSETS "${WEBGPU_SAMPLES_ASSETS}/webgpu.png|webgpu.png"
    )

    pe_add_webgpu_sample(
        TARGET WebGPUMarchingCubesTest
        DIR MarchingCubes
        SOURCE MarchingCubesTest.cpp
        USE_COMMON_APP
        SHADERS generate.comp surface.vert surface.pixel
        ASSETS
            "${CMAKE_SOURCE_DIR}/../webgpu-marchingcubes/textures/coast_sand_rocks_02_diff_1k.jpg|coast_sand_rocks_02_diff_1k.jpg"
            "${CMAKE_SOURCE_DIR}/../webgpu-marchingcubes/textures/mud_cracked_dry_03_diff_1k.jpg|mud_cracked_dry_03_diff_1k.jpg"
            "${CMAKE_SOURCE_DIR}/../webgpu-marchingcubes/textures/rock_face_03_diff_1k.jpg|rock_face_03_diff_1k.jpg"
            "${CMAKE_SOURCE_DIR}/../webgpu-marchingcubes/textures/coast_sand_rocks_02_nor_gl_1k.png|coast_sand_rocks_02_nor_gl_1k.png"
            "${CMAKE_SOURCE_DIR}/../webgpu-marchingcubes/textures/mud_cracked_dry_03_nor_gl_1k.png|mud_cracked_dry_03_nor_gl_1k.png"
            "${CMAKE_SOURCE_DIR}/../webgpu-marchingcubes/textures/rock_face_03_nor_gl_1k.png|rock_face_03_nor_gl_1k.png"
    )
endif()
