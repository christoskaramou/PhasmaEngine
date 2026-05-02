#pragma once

#include "API/Swapchain.h"

namespace pe
{
    struct Swapchain::Impl : public NoCopy
    {
        virtual ~Impl() = default;
        virtual uint32_t AquireNextImage(Semaphore *semaphore) = 0;
    };

    Swapchain::Impl *CreateSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc);
} // namespace pe
