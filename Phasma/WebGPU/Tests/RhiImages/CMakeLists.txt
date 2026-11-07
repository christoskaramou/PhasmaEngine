# Browser-only test of the WebGPU RHI backend's images, samplers and bind groups (spec P2). Output: RhiImages.html/.js/.wasm.
add_executable(RhiImages RhiImages.cpp)
target_precompile_headers(RhiImages PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_link_libraries(RhiImages PRIVATE PhasmaCore)
set_target_properties(RhiImages PROPERTIES SUFFIX ".html")
target_link_options(RhiImages PRIVATE
    -sJSPI
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4MB
)
