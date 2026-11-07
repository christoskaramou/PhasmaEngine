# Browser-only test of the WebGPU RHI backend's surface and swapchain (spec P2). Output: RhiSwapchain.html/.js/.wasm.
add_executable(RhiSwapchain RhiSwapchain.cpp)
target_precompile_headers(RhiSwapchain PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_link_libraries(RhiSwapchain PRIVATE PhasmaCore)
set_target_properties(RhiSwapchain PROPERTIES SUFFIX ".html")
target_link_options(RhiSwapchain PRIVATE
    -sJSPI
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4MB
)
