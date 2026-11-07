# Browser-only test of the WebGPU RHI backend's buffer rules (spec P2). Output: RhiBufferRules.html/.js/.wasm.
add_executable(RhiBufferRules RhiBufferRules.cpp)
target_precompile_headers(RhiBufferRules PRIVATE "${CMAKE_SOURCE_DIR}/Phasma/Core/pch/PhasmaPch.h")
target_link_libraries(RhiBufferRules PRIVATE PhasmaCore)
set_target_properties(RhiBufferRules PROPERTIES SUFFIX ".html")
target_link_options(RhiBufferRules PRIVATE
    -sJSPI
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4MB
)
