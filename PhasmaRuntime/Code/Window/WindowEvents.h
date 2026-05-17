#pragma once

#include "SDL.h"

#include <cstdint>

namespace pe
{
    struct WindowDrawableExtent
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] bool IsValid() const { return width > 0 && height > 0; }
    };

    [[nodiscard]] WindowDrawableExtent GetWindowDrawableExtent(SDL_Window *window);
    [[nodiscard]] bool IsRuntimeWindowResizeEvent(uint8_t windowEvent);
    [[nodiscard]] bool IsRuntimeWindowResizeEvent(const SDL_Event &event);
    [[nodiscard]] bool IsWindowMinimized(SDL_Window *window);
} // namespace pe
