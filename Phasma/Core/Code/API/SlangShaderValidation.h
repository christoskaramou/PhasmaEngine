#pragma once

#if defined(PE_SHADER_COMPILER_SLANG)

#include "API/Shader.h"

namespace pe
{
    // PE_SHADER_COMPILER_SLANG: compiles the exact source DXC just compiled with slangc and logs any
    // descriptor-binding / push-constant difference against the DXC bytecode. DXC output stays authoritative.
    void ValidateShaderWithSlang(Shader *owner,
                                 PeShaderStageFlags stage,
                                 const std::string &source,
                                 const std::vector<Define> &globalDefines,
                                 const std::vector<Define> &localDefines,
                                 bool dxil,
                                 const void *dxcBytecode,
                                 size_t dxcSizeBytes);
} // namespace pe

#endif
