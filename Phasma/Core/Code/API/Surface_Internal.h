#pragma once

#include "API/Surface.h"

namespace pe
{
    struct Surface::Impl : public NoCopy
    {
        virtual ~Impl() = default;

        virtual std::vector<PePresentMode> GetSupportedPresentModes() const = 0;
        virtual bool IsPresentable() const = 0;
    };

    Surface::Impl *CreateSurfaceImpl(Surface *owner, SDL_Window *window);
} // namespace pe
