#include "Window/WindowEvents.h"

namespace pe
{
    WindowDrawableExtent GetWindowDrawableExtent(SDL_Window *window)
    {
        WindowDrawableExtent extent{};
        if (window)
            SDL_GetWindowSizeInPixels(window, &extent.width, &extent.height);
        return extent;
    }

    bool IsRuntimeWindowEventFor(const SDL_Event &event, SDL_Window *window)
    {
        return window && event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(window);
    }

    bool IsRuntimeWindowDisplayChangedEvent(uint8_t windowEvent)
    {
        return windowEvent == SDL_WINDOWEVENT_DISPLAY_CHANGED;
    }

    bool IsRuntimeWindowDisplayChangedEvent(const SDL_Event &event)
    {
        return event.type == SDL_WINDOWEVENT && IsRuntimeWindowDisplayChangedEvent(event.window.event);
    }

    bool IsRuntimeWindowResizeEvent(uint8_t windowEvent)
    {
        switch (windowEvent)
        {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_MAXIMIZED:
        case SDL_WINDOWEVENT_RESTORED:
            return true;
        default:
            return false;
        }
    }

    bool IsRuntimeWindowResizeEvent(const SDL_Event &event)
    {
        return event.type == SDL_WINDOWEVENT && IsRuntimeWindowResizeEvent(event.window.event);
    }

    bool IsWindowMinimized(SDL_Window *window)
    {
        return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
    }

} // namespace pe
