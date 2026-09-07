#include "Runtime/RuntimeHost.h"
#include "API/RHI.h"
#include "Base/WindowIcon.h"

namespace pe
{
    bool IsWslEnvironment()
    {
#if defined(PE_WIN32)
        return false;
#else
        static const bool isWsl = std::getenv("WSL_DISTRO_NAME") || std::getenv("WSL_INTEROP");
        return isWsl;
#endif
    }

    void ApplyWslSdlVideoHints()
    {
        if (!IsWslEnvironment())
            return;
        if (!std::getenv("SDL_VIDEODRIVER"))
            SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11");
        if (!std::getenv("SDL_VIDEO_X11_FORCE_EGL"))
            SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "0");
        if (!std::getenv("SDL_RENDER_DRIVER"))
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    }

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
        // Launcher X11+GLX hints are for SDL_Renderer only. Vulkan/Dozen on
        // WSLg presents through the Wayland wl_surface.
        if (SDL_Init(initFlags) < 0)
            PE_ERROR("[SDL] %s", SDL_GetError());
        m_initialized = true;

        const char *driver = SDL_GetCurrentVideoDriver();
        PE_INFO("[SDL] video driver: %s", driver ? driver : "(null)");
        if (IsWslEnvironment() && !std::filesystem::exists("/mnt/shared_memory"))
            PE_WARN("[WSL] /mnt/shared_memory is missing; WSLg is in COPY MODE and "
                    "Linux windows will not appear. From Windows run: wsl --shutdown");
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

        int windowWidth = displayBounds.w > 100 ? displayBounds.w - 100 : displayBounds.w;
        int windowHeight = displayBounds.h > 100 ? displayBounds.h - 100 : displayBounds.h;
        int windowX = SDL_WINDOWPOS_CENTERED_DISPLAY(desc.displayIndex);
        int windowY = SDL_WINDOWPOS_CENTERED_DISPLAY(desc.displayIndex);
        uint32_t windowFlags = desc.flags;
        if (desc.api == PE_GRAPHICS_API_VULKAN)
            windowFlags |= SDL_WINDOW_VULKAN;
        const bool wsl = IsWslEnvironment();
        if (wsl)
        {
            // WSLg never maps a window created hidden, then shown.
            if (desc.showAfterCreate)
                windowFlags &= ~SDL_WINDOW_HIDDEN;
            // WSLg reports spanned monitors as one display (5120x1440 here).
            // Maximize / a full-span client leaves no focusable RAIL surface.
            windowFlags &= ~SDL_WINDOW_MAXIMIZED;
            if (windowWidth > 1920)
                windowWidth = 1920;
            if (windowHeight > 1080)
                windowHeight = 1080;
            windowX = displayBounds.x + 48;
            windowY = displayBounds.y + 48;
        }

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
                                    windowX,
                                    windowY,
                                    windowWidth,
                                    windowHeight,
                                    windowFlags);
        if (!m_window)
            PE_ERROR("[SDL] %s", SDL_GetError());

        SetPhasmaWindowIcon(m_window);

        if (desc.showAfterCreate)
            SDL_ShowWindow(m_window);
        if (desc.maximizeAfterCreate && !wsl)
            SDL_MaximizeWindow(m_window);
        if (wsl && desc.showAfterCreate)
            SDL_RaiseWindow(m_window);
        if (desc.pumpEventsAfterCreate)
            SDL_PumpEvents();

        if (desc.logDisplaySelection)
        {
            int x = 0, y = 0, w = 0, h = 0;
            SDL_GetWindowPosition(m_window, &x, &y);
            SDL_GetWindowSize(m_window, &w, &h);
            PE_INFO("[SDL] window %dx%d at (%d, %d) flags=0x%x", w, h, x, y, SDL_GetWindowFlags(m_window));
        }
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
