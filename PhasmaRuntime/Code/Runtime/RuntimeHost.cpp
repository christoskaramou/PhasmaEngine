#include "Runtime/RuntimeHost.h"
#include "API/RHI.h"
#include "Base/Log.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace pe
{
    bool TryParseRuntimeDisplayIndex(const char *value, int &displayIndex)
    {
        if (!value || *value == '\0')
            return false;

        char *end = nullptr;
        long parsed = std::strtol(value, &end, 10);
        if (*end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max())
            return false;

        displayIndex = static_cast<int>(parsed);
        return true;
    }

    bool TryParseRuntimeDisplayIndexArg(int argc, char *argv[], int &displayIndex, std::string *error)
    {
        for (int i = 1; i < argc; ++i)
        {
            if ((std::strcmp(argv[i], "--display") == 0 || std::strcmp(argv[i], "--screen") == 0) && i + 1 < argc)
            {
                const char *value = argv[++i];
                if (!TryParseRuntimeDisplayIndex(value, displayIndex))
                {
                    if (error)
                        *error = std::string("Invalid display index: ") + value;
                    return false;
                }
            }
        }

        return true;
    }

    RuntimeSdlSession::RuntimeSdlSession(uint32_t initFlags)
    {
        if (SDL_Init(initFlags) < 0)
            PE_ERROR("[SDL] %s", SDL_GetError());
        m_initialized = true;
    }

    RuntimeSdlSession::~RuntimeSdlSession()
    {
        if (m_initialized)
            SDL_Quit();
    }

    RuntimeWindow::RuntimeWindow(const RuntimeWindowDesc &desc)
    {
        const int displayCount = SDL_GetNumVideoDisplays();
        if (displayCount <= 0)
            PE_ERROR("[SDL] no video displays found: %s", SDL_GetError());

        if (desc.displayIndex < 0 || desc.displayIndex >= displayCount)
            PE_ERROR("Invalid --display %d; SDL reports %d display(s)", desc.displayIndex, displayCount);

        SDL_Rect displayBounds{};
        if (SDL_GetDisplayBounds(desc.displayIndex, &displayBounds) != 0)
            PE_ERROR("[SDL] SDL_GetDisplayBounds(%d) failed: %s", desc.displayIndex, SDL_GetError());

        const int windowWidth = displayBounds.w > 100 ? displayBounds.w - 100 : displayBounds.w;
        const int windowHeight = displayBounds.h > 100 ? displayBounds.h - 100 : displayBounds.h;
        uint32_t windowFlags = desc.flags;
        if (desc.api == PE_GRAPHICS_API_VULKAN)
            windowFlags |= SDL_WINDOW_VULKAN;

        if (desc.logDisplaySelection)
        {
            PE_INFO("Creating window on display %d/%d at (%d, %d) size %dx%d",
                    desc.displayIndex,
                    displayCount,
                    displayBounds.x,
                    displayBounds.y,
                    displayBounds.w,
                    displayBounds.h);
        }

        m_window = SDL_CreateWindow(desc.title ? desc.title : "Phasma",
                                    SDL_WINDOWPOS_CENTERED_DISPLAY(desc.displayIndex),
                                    SDL_WINDOWPOS_CENTERED_DISPLAY(desc.displayIndex),
                                    windowWidth,
                                    windowHeight,
                                    windowFlags);
        if (!m_window)
            PE_ERROR("[SDL] %s", SDL_GetError());

        if (desc.showAfterCreate)
            SDL_ShowWindow(m_window);
        if (desc.maximizeAfterCreate)
            SDL_MaximizeWindow(m_window);
        if (desc.pumpEventsAfterCreate)
            SDL_PumpEvents();
    }

    RuntimeWindow::~RuntimeWindow()
    {
        if (m_window)
            SDL_DestroyWindow(m_window);
    }

    RuntimeRhiSession::RuntimeRhiSession(SDL_Window *window, PeGraphicsApi api, bool initializeSwapchain)
    {
        RHII.Init(window, api);
        m_initialized = true;
        if (initializeSwapchain)
            RHII.InitSwapchain();
    }

    RuntimeRhiSession::~RuntimeRhiSession()
    {
        if (m_initialized)
            RHII.Destroy();
    }
} // namespace pe
