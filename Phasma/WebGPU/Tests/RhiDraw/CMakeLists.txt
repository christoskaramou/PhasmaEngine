# Browser-only P2 exit test of the WebGPU RHI backend: passes, pipelines, draws, a dispatch and push constants
# (spec P2). Output: RhiDraw.html/.js/.wasm. The test page writes the baked shaders into MEMFS (FS) before main.
add_executable(RhiDraw RhiDraw.cpp)
target_precompile_headers(RhiDraw PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_link_libraries(RhiDraw PRIVATE PhasmaCore)
set_target_properties(RhiDraw PROPERTIES SUFFIX ".html")
target_link_options(RhiDraw PRIVATE
    -sJSPI
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4MB
    -sEXPORTED_RUNTIME_METHODS=FS
)
