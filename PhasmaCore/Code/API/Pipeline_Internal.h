#pragma once

#include "API/Pipeline.h"

namespace pe
{
    struct Pipeline::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    Pipeline::Impl *CreatePipelineImpl(Pipeline *owner, RenderPass *renderPass, PassInfo &info);
} // namespace pe
