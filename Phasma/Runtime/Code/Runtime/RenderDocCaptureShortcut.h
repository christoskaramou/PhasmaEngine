#pragma once

#include <SDL_events.h>

namespace pe
{
    [[nodiscard]] bool IsRenderDocCaptureShortcut(const SDL_Event &event);
    void TriggerRenderDocCaptureShortcut();
} // namespace pe
