#pragma once


namespace pe
{
    inline bool IsRuntimeUiMouseInputEvent(const SDL_Event &event)
    {
        return event.type == SDL_MOUSEMOTION ||
               event.type == SDL_MOUSEBUTTONDOWN ||
               event.type == SDL_MOUSEBUTTONUP ||
               event.type == SDL_MOUSEWHEEL;
    }

    inline bool IsRuntimeUiKeyboardInputEvent(const SDL_Event &event)
    {
        return event.type == SDL_KEYDOWN ||
               event.type == SDL_KEYUP ||
               event.type == SDL_TEXTINPUT ||
               event.type == SDL_TEXTEDITING;
    }

    inline bool IsRuntimeUiTextInputEvent(const SDL_Event &event)
    {
        return event.type == SDL_TEXTINPUT ||
               event.type == SDL_TEXTEDITING;
    }
} // namespace pe
