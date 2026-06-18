#include "Runtime/RenderDocCaptureShortcut.h"
#include "API/Debug.h"

namespace pe
{
    bool IsRenderDocCaptureShortcut(const SDL_Event &event)
    {
        return event.type == SDL_KEYDOWN &&
               event.key.repeat == 0 &&
               event.key.keysym.sym == SDLK_t &&
               (event.key.keysym.mod & KMOD_CTRL) != 0;
    }

    void TriggerRenderDocCaptureShortcut()
    {
        if (!Debug::IsCaptureApiAvailable())
        {
            PE_WARN("[RenderDoc] Ctrl+T capture requested, but RenderDoc is not available");
            return;
        }

        PE_INFO("[RenderDoc] Ctrl+T capture requested");
        Debug::TriggerMultiFrameCapture(1);
    }
} // namespace pe
