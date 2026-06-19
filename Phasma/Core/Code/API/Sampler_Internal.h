#pragma once

#include "API/Sampler.h"

namespace pe
{
    struct Sampler::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    Sampler::Impl *CreateSamplerImpl(Sampler *owner, const SamplerDesc &desc);
} // namespace pe
