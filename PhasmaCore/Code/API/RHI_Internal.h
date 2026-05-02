#pragma once

#include "API/RHI.h"

namespace pe
{
    // State migration into the Impl ships with the DX12 backend in Phase 1.
    struct RHI::Impl : public NoCopy
    {
        virtual ~Impl() = default;
        virtual void WaitDeviceIdle() = 0;
    };
} // namespace pe
