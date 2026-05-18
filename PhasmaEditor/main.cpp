#include "Base/Log.h"
#include "Base/Path.h"
#include "Base/EventSystem.h"
#include "Base/ThreadPool.h"
#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"
#include "Runtime/RuntimeHost.h"

#if defined(PE_LINUX)
#include <dlfcn.h>
static constexpr const char *k_moduleName = "libPhasmaEditorModule.so";
#elif defined(PE_WIN32)
#include <windows.h>
static constexpr const char *k_moduleName = "PhasmaEditorModule.dll";
#endif

using TickFunc = bool (*)();
using RenderReloadFunc = void (*)();
using DestroyFunc = void (*)();
using GetImGuiCtxFunc = void *(*)();
using InitWithCtxFunc = void (*)(void *);

namespace
{
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
        const std::filesystem::path modulePath = std::filesystem::path(pe::Path::Executable) / k_moduleName;
#if defined(PE_LINUX)
        static int s_gen = 0;
        char versioned[256];
        std::snprintf(versioned, sizeof(versioned), "libPhasmaEditorModule_%04d.so", s_gen++);
        const std::filesystem::path versionedPath = std::filesystem::path(pe::Path::Executable) / versioned;
        std::error_code ec;
        std::filesystem::copy_file(modulePath, versionedPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            PE_ERROR("copy_file failed: %s", ec.message().c_str());
            return m;
        }
        m.loadedPath = versionedPath.string();
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
        const std::filesystem::path versionedPath = std::filesystem::path(pe::Path::Executable) / versioned;
        if (!CopyFileA(modulePath.string().c_str(), versionedPath.string().c_str(), FALSE))
        {
            PE_ERROR("CopyFileA failed: %lu", ::GetLastError());
            return m;
        }
        m.loadedPath = versionedPath.string();
        m.lib = static_cast<void *>(::LoadLibraryA(m.loadedPath.c_str()));
        if (!m.lib)
        {
            PE_ERROR("LoadLibraryA failed: %lu", ::GetLastError());
            return m;
        }
        m.tick = reinterpret_cast<TickFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "TickEditorModule"));
        m.renderReload = reinterpret_cast<RenderReloadFunc>(
            ::GetProcAddress(static_cast<HMODULE>(m.lib), "RenderReloadFrameEditorModule"));
        m.destroy = reinterpret_cast<DestroyFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "DestroyEditorModule"));
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

        PeGraphicsApi api = apiSelection.api;
        int displayIndex = 0;
        std::string displayError;
        if (!pe::TryParseRuntimeDisplayIndexArg(argc, argv, displayIndex, &displayError))
        {
            PE_ERROR("%s", displayError.c_str());
            return 1;
        }

        PE_INFO("Selected graphics API: %s (%s)",
                PeGraphicsApiName(api),
                pe::GraphicsApiSelectionSourceName(apiSelection.source));

        // SDL and graphics device live here; they survive module reloads.
        pe::RuntimeSdlSession sdl;

        pe::RuntimeWindowDesc windowDesc;
        windowDesc.title = "PhasmaEditor";
        windowDesc.api = api;
        windowDesc.displayIndex = displayIndex;
        windowDesc.flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if !defined(PE_WIN32)
        // Sizes the surface/swapchain to the maximized extent at creation time so
        // App.cpp's early Show()/Maximize() doesn't trigger a swapchain recreate.
        windowDesc.flags |= SDL_WINDOW_MAXIMIZED;
#endif
        // Show/maximize before RHI init creates the Vulkan surface/swapchain.
        // Windows WSI caches a redirected present path on a hidden HWND; Mesa
        // Dozen on WSLg registers its WSLg-mirror handle on the wl_surface at
        // swapchain create — both need a visible window first.
        windowDesc.showAfterCreate = true;
        windowDesc.maximizeAfterCreate = true;
        windowDesc.pumpEventsAfterCreate = true;
        windowDesc.logDisplaySelection = true;
        pe::RuntimeWindow window(windowDesc);

        pe::RuntimeRhiSession rhi(window.Get(), api, false);
#if !defined(PE_WIN32)
        PE_INFO("[Startup] Dozen Vulkan after RHI init: %u", pe::RHII.UsesDozenVulkan() ? 1u : 0u);
        if (pe::RHII.UsesDozenVulkan())
        {
            PE_INFO("[Startup] Initializing Dozen swapchain before editor module load");
            pe::RHII.InitSwapchain();
        }
#endif

        ModuleHandle mod = LoadModule();
        if (!mod.lib)
            return 1;

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
        return 0;
    }
    catch (const std::exception &e)
    {
        pe::Log::Error(std::string("Unhandled exception in editor host: ") + e.what());
    }
    catch (...)
    {
        pe::Log::Error("Unhandled non-standard exception in editor host");
    }
    return 1;
}
