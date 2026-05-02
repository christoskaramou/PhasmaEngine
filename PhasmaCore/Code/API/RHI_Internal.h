#pragma once

#include "API/RHI.h"

namespace pe
{
    struct RHI::Impl : public NoCopy
    {
        virtual ~Impl() = default;
        virtual bool Init(SDL_Window *window) = 0;
        virtual void Shutdown() = 0;
        virtual void WaitDeviceIdle() = 0;
        virtual void NextFrame() = 0;
    };
} // namespace pe
