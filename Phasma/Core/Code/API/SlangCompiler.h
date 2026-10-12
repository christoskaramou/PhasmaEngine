#pragma once

#if defined(PE_RUNTIME_SHADER_COMPILER)

#include "API/Shader.h"

namespace pe
{
    enum class SlangCompileStatus : uint8_t
    {
        Ok,
        NotStarted, // slangc missing (PE_SLANGC_PATH)
        Failed
    };

    // Compiles the exact source and defines the DXC path would use with slangc, to SPIR-V for a "spirv_1_x" profile
    // or DXIL for "sm_6_x". On failure `log` holds slangc's output.
    SlangCompileStatus CompileWithSlang(Shader *owner,
                                        PeShaderStageFlags stage,
                                        const std::string &source,
                                        const std::vector<Define> &globalDefines,
                                        const std::vector<Define> &localDefines,
                                        const char *profile,
                                        std::vector<uint8_t> &out,
                                        std::string &log);

#if defined(PE_SLANG_VALIDATION)
    // PE_SLANG_VALIDATION: compiles what DXC just compiled with slangc too and logs any descriptor-binding or
    // byte-layout difference against the DXC bytecode. DXC output stays authoritative.
    void ValidateShaderWithSlang(Shader *owner,
                                 PeShaderStageFlags stage,
                                 const std::string &source,
                                 const std::vector<Define> &globalDefines,
                                 const std::vector<Define> &localDefines,
                                 const char *profile,
                                 const void *dxcBytecode,
                                 size_t dxcSizeBytes);
#endif
} // namespace pe

#endif
