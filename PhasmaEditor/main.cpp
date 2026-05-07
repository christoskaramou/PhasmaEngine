#include "Base/Log.h"
#include "Base/EventSystem.h"
#include "Base/ThreadPool.h"
#include "API/RHI.h"

#if defined(PE_LINUX)
#include <dlfcn.h>
static constexpr const char *k_moduleName = "libPhasmaEditorModule.so";
#elif defined(PE_WIN32)
static constexpr const char *k_moduleName = "PhasmaEditorModule.dll";
#endif

using TickFunc = bool (*)();
using RenderReloadFunc = void (*)();
using DestroyFunc = void (*)();
using GetImGuiCtxFunc = void *(*)();
using InitWithCtxFunc = void (*)(void *);

namespace
{
    bool ParseDisplayIndex(const char *value, int &displayIndex)
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

    struct ModuleHandle
    {
        void *lib = nullptr;
        TickFunc tick = nullptr;
        RenderReloadFunc renderReload = nullptr;
        DestroyFunc destroy = nullptr;
        GetImGuiCtxFunc getImguiCtx = nullptr;
        InitWithCtxFunc initWithCtx = nullptr;
        std::string loadedPath;
    };

    ModuleHandle LoadModule()
    {
        ModuleHandle m;
#if defined(PE_LINUX)
        static int s_gen = 0;
        char versioned[256];
        std::snprintf(versioned, sizeof(versioned), "libPhasmaEditorModule_%04d.so", s_gen++);
        std::error_code ec;
        std::filesystem::copy_file(k_moduleName, versioned, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            PE_ERROR("copy_file failed: %s", ec.message().c_str());
            return m;
        }
        m.loadedPath = versioned;
        m.lib = dlopen(m.loadedPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m.lib)
        {
            PE_ERROR("dlopen failed: %s", dlerror());
            return m;
        }
        m.tick = reinterpret_cast<TickFunc>(dlsym(m.lib, "TickEditorModule"));
        m.renderReload = reinterpret_cast<RenderReloadFunc>(dlsym(m.lib, "RenderReloadFrameEditorModule"));
        m.destroy = reinterpret_cast<DestroyFunc>(dlsym(m.lib, "DestroyEditorModule"));
        m.getImguiCtx = reinterpret_cast<GetImGuiCtxFunc>(dlsym(m.lib, "GetImGuiContextEditorModule"));
        m.initWithCtx = reinterpret_cast<InitWithCtxFunc>(dlsym(m.lib, "InitEditorModuleWithContext"));
#elif defined(PE_WIN32)
        static int s_gen = 0;
        char versioned[256];
        std::snprintf(versioned, sizeof(versioned), "PhasmaEditorModule_%04d.dll", s_gen++);
        CopyFileA(k_moduleName, versioned, FALSE);
        m.loadedPath = versioned;
        m.lib = static_cast<void *>(::LoadLibraryA(versioned));
        if (!m.lib)
        {
            PE_ERROR("LoadLibraryA failed: %lu", ::GetLastError());
            return m;
        }
        m.tick =
            reinterpret_cast<TickFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "TickEditorModule"));
        m.renderReload =
            reinterpret_cast<RenderReloadFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "RenderReloadFrameEditorModule"));
        m.destroy =
            reinterpret_cast<DestroyFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "DestroyEditorModule"));
        m.getImguiCtx =
            reinterpret_cast<GetImGuiCtxFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "GetImGuiContextEditorModule"));
        m.initWithCtx =
            reinterpret_cast<InitWithCtxFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "InitEditorModuleWithContext"));
#endif
        if (!m.tick || !m.renderReload || !m.destroy)
        {
            PE_ERROR("Failed to resolve module entry points");
#if defined(PE_LINUX)
            dlclose(m.lib);
#elif defined(PE_WIN32)
            ::FreeLibrary(static_cast<HMODULE>(m.lib));
#endif
            m.lib = nullptr;
        }
        return m;
    }

    void UnloadModule(ModuleHandle &m)
    {
        if (!m.lib)
            return;
#if defined(PE_LINUX)
        dlclose(m.lib);
#elif defined(PE_WIN32)
        ::FreeLibrary(static_cast<HMODULE>(m.lib));
#endif
        if (!m.loadedPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(m.loadedPath, ec);
        }
        m = {};
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();

    try
    {
        PeGraphicsApi api = PE_GRAPHICS_API_VULKAN;
        int displayIndex = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--api") == 0 && i + 1 < argc)
            {
                const char *apiArg = argv[++i];
                if (std::strcmp(apiArg, "vulkan") == 0)
                {
                    api = PE_GRAPHICS_API_VULKAN;
                }
                else if (std::strcmp(apiArg, "dx12") == 0)
                {
#if defined(PE_WIN32)
                    api = PE_GRAPHICS_API_DX12;
#else
                    PE_ERROR("DX12 backend is Windows-only; use --api vulkan");
                    return 1;
#endif
                }
                else
                {
                    PE_ERROR("Unknown --api value: %s (expected: vulkan, dx12)", apiArg);
                    return 1;
                }
            }
            else if ((std::strcmp(argv[i], "--display") == 0 || std::strcmp(argv[i], "--screen") == 0) && i + 1 < argc)
            {
                if (!ParseDisplayIndex(argv[++i], displayIndex))
                {
                    PE_ERROR("Invalid display index: %s", argv[i]);
                    return 1;
                }
            }
        }

        // api = PE_GRAPHICS_API_DX12;
        // displayIndex = 1;

        // SDL and graphics device live here — they survive module reloads.
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        {
            PE_ERROR("[SDL] %s", SDL_GetError());
            return 1;
        }

        const int displayCount = SDL_GetNumVideoDisplays();
        if (displayCount <= 0)
        {
            PE_ERROR("[SDL] no video displays found: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        if (displayIndex >= displayCount)
        {
            PE_ERROR("Invalid --display %d; SDL reports %d display(s)", displayIndex, displayCount);
            SDL_Quit();
            return 1;
        }

        SDL_Rect displayBounds{};
        if (SDL_GetDisplayBounds(displayIndex, &displayBounds) != 0)
        {
            PE_ERROR("[SDL] SDL_GetDisplayBounds(%d) failed: %s", displayIndex, SDL_GetError());
            SDL_Quit();
            return 1;
        }

        int windowWidth = displayBounds.w > 100 ? displayBounds.w - 100 : displayBounds.w;
        int windowHeight = displayBounds.h > 100 ? displayBounds.h - 100 : displayBounds.h;
        uint32_t windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                               SDL_WINDOW_MAXIMIZED; // Sizes initial DX12 swapchain before the existing maximize call.
        if (api == PE_GRAPHICS_API_VULKAN)
            windowFlags |= SDL_WINDOW_VULKAN;
        PE_INFO("Creating window on display %d/%d at (%d, %d) size %dx%d",
                displayIndex, displayCount, displayBounds.x, displayBounds.y, displayBounds.w, displayBounds.h);
        SDL_Window *sdlWindow = SDL_CreateWindow("PhasmaEditor",
                                                 SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                                 SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                                                 windowWidth,
                                                 windowHeight,
                                                 windowFlags);
        if (!sdlWindow)
        {
            PE_ERROR("[SDL] %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        pe::RHII.Init(sdlWindow, api);

        ModuleHandle mod = LoadModule();
        if (!mod.lib)
        {
            pe::RHII.Destroy();
            SDL_DestroyWindow(sdlWindow);
            SDL_Quit();
            return 1;
        }

        while (true)
        {
            if (!mod.tick())
            {
                mod.destroy();
                break;
            }

            pe::EventSystem::QueuedEvent ev;
            if (pe::EventSystem::PeekAndPop(pe::EventType::ReloadModule, ev))
            {
                mod.renderReload(); // drain in-flight frames and show "Reloading..." overlay
                void *imguiCtx = mod.getImguiCtx ? mod.getImguiCtx() : nullptr;
                std::ofstream(pe::Path::Executable + "reload.flag").close();
                mod.destroy();
                pe::ThreadPool::FW.WaitIdle();
                UnloadModule(mod);
                mod = LoadModule();
                if (!mod.lib)
                {
                    PE_ERROR("Reload failed");
                    break;
                }
                if (imguiCtx && mod.initWithCtx)
                    mod.initWithCtx(imguiCtx);
            }
        }

        UnloadModule(mod);

        pe::RHII.Destroy();
        SDL_DestroyWindow(sdlWindow);
        SDL_Quit();
        return 0;
    }
    catch (const std::exception &e)
    {
        pe::Log::Error(std::string("Unhandled exception in launcher: ") + e.what());
    }
    catch (...)
    {
        pe::Log::Error("Unhandled non-standard exception in launcher");
    }
    return 1;
}
