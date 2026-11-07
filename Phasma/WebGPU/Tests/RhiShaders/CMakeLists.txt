# Browser-only test of the WebGPU RHI backend's baked shaders (spec P2, decision 7). Output: RhiShaders.html/.js/.wasm.
# The test page writes the sources and their baked ShaderCache files into MEMFS (FS) before main.
add_executable(RhiShaders RhiShaders.cpp)
target_precompile_headers(RhiShaders PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_link_libraries(RhiShaders PRIVATE PhasmaCore)
set_target_properties(RhiShaders PROPERTIES SUFFIX ".html")
target_link_options(RhiShaders PRIVATE
    -sJSPI
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4MB
    -sEXPORTED_RUNTIME_METHODS=FS
)
