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

namespace
{
    struct ModuleHandle
    {
        void *lib = nullptr;
        TickFunc tick = nullptr;
        RenderReloadFunc renderReload = nullptr;
        DestroyFunc destroy = nullptr;
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

    // SDL and Vulkan device live here — they survive module reloads.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        PE_ERROR("[SDL] %s", SDL_GetError());
        return 1;
    }

    SDL_DisplayMode dm;
    SDL_GetDesktopDisplayMode(0, &dm);
    uint32_t windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;
    SDL_Window *sdlWindow = SDL_CreateWindow("PhasmaEditor", 100, 100, dm.w - 100, dm.h - 100, windowFlags);
    if (!sdlWindow)
    {
        PE_ERROR("[SDL] %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    pe::RHII.Init(sdlWindow);

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
        }
    }

    UnloadModule(mod);

    pe::RHII.Destroy();
    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
    return 0;
}
