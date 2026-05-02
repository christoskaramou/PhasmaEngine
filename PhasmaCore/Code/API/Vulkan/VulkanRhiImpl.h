#pragma once

#include "API/RHI_Internal.h"

namespace pe
{
    struct VulkanRhiImpl final : public RHI::Impl
    {
        void WaitDeviceIdle() override;
    };
} // namespace pe
