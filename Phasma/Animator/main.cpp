#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "AnimatorApp.h"
#include "Runtime/RuntimeHost.h"

int main(int argc, char *argv[])
{
    pe::Path::Init();
    pe::Log::Init();

    try
    {
        const pe::GraphicsApiSelection apiSelection = pe::ResolveGraphicsApi(argc, argv);
        if (!apiSelection.Succeeded())
        {
            PE_ERROR("%s", apiSelection.error.c_str());
            return 1;
        }
        int displayIndex = 0;
        std::string displayError;
        if (!pe::TryParseRuntimeDisplayIndexArg(argc, argv, displayIndex, &displayError))
        {
            PE_ERROR("%s", displayError.c_str());
            return 1;
        }
        PE_INFO("Selected graphics API: %s (%s)", PeGraphicsApiName(apiSelection.api),
                pe::GraphicsApiSelectionSourceName(apiSelection.source));

        pe::RuntimeSdlSession sdl;
        pe::EventSystem::Init();
        pe::RuntimeWindowDesc windowDesc;
        windowDesc.title = "Phasma Animator";
        windowDesc.api = apiSelection.api;
        windowDesc.displayIndex = displayIndex;
        windowDesc.flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if !defined(PE_WIN32)
        windowDesc.flags |= SDL_WINDOW_MAXIMIZED;
#endif
        // Shown before the RHI creates the surface: Windows WSI needs a visible HWND at swapchain creation.
        windowDesc.showAfterCreate = true;
        windowDesc.maximizeAfterCreate = true;
        windowDesc.pumpEventsAfterCreate = true;
        windowDesc.logDisplaySelection = true;
        pe::RuntimeWindow window(windowDesc);
        pe::RuntimeRhiSession rhi(window.Get(), apiSelection.api, false);
        pe::RHII.GetSurface()->SetPresentMode(PE_PRESENT_MODE_FIFO);
        pe::Settings::Get<pe::SceneSettings>().preferred_present_mode = pe::RHII.GetSurface()->GetPresentMode();
        pe::RHII.InitSwapchain();
        {
            pe::AnimatorApp app(argc, argv);
            while (app.Frame())
            {
            }
        }
        pe::EventSystem::Destroy();
        return 0;
    }
    catch (const std::exception &e)
    {
        pe::Log::Error(std::string("Unhandled exception in PhasmaAnimator: ") + e.what());
    }
    catch (...)
    {
        pe::Log::Error("Unhandled non-standard exception in PhasmaAnimator");
    }
    return 1;
}
