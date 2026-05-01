#pragma once

#include "API/Shader.h"

namespace pe
{
    struct Shader::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    Shader::Impl *CreateShaderImpl(Shader *owner, const ShaderDesc &desc);
    Shader::Impl *CreateShaderImplFromSpirv(Shader *owner,
                                            const uint32_t *spirv,
                                            size_t sizeWords);
} // namespace pe
