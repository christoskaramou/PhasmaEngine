#pragma once

#include "API/Shader.h"

namespace pe
{
    struct Shader::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    Shader::Impl *CreateShaderImpl(Shader *owner, const ShaderDesc &desc);
} // namespace pe
