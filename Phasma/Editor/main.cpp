#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"
#include "Runtime/RuntimeHost.h"

#if defined(PE_LINUX)
#include <dlfcn.h>
static constexpr const char *k_moduleName = "libPhasmaEditorModule.so";
static constexpr const char *k_versionedModulePrefix = "libPhasmaEditorModule_";
static constexpr const char *k_versionedModuleSuffix = ".so";
#elif defined(PE_WIN32)
#include <windows.h>
static constexpr const char *k_moduleName = "PhasmaEditorModule.dll";
static constexpr const char *k_versionedModulePrefix = "PhasmaEditorModule_";
static constexpr const char *k_versionedModuleSuffix = ".dll";
#endif

using TickFunc = bool (*)();
using RenderReloadFunc = void (*)();
using DestroyFunc = void (*)();

namespace
{
    enum class ModuleUnloadMode
    {
        Reload,
        ProcessExit,
    };

    struct ModuleHandle
    {
        void *lib = nullptr;
        TickFunc tick = nullptr;
        RenderReloadFunc renderReload = nullptr;
        DestroyFunc destroy = nullptr;
        std::string loadedPath;
    };

    bool IsVersionedModuleCopy(const std::filesystem::path &path)
    {
        const std::string filename = path.filename().string();
        const std::string prefix = k_versionedModulePrefix;
        const std::string suffix = k_versionedModuleSuffix;

        if (filename.size() <= prefix.size() + suffix.size())
            return false;
        if (filename.rfind(prefix, 0) != 0)
            return false;
        if (filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0)
            return false;

        const std::string generation = filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());
        return !generation.empty() &&
               std::all_of(generation.begin(), generation.end(), [](unsigned char c)
                           { return std::isdigit(c) != 0; });
    }

    void RemoveStaleModuleCopies(const std::filesystem::path &directory)
    {
        std::error_code ec;
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec))
            {
                ec.clear();
                continue;
            }
            if (!IsVersionedModuleCopy(entry.path()))
                continue;

            std::filesystem::remove(entry.path(), ec);
            ec.clear();
        }
    }

    ModuleHandle LoadModule()
    {
        ModuleHandle m;
        const std::filesystem::path modulePath = std::filesystem::path(pe::Path::Executable) / k_moduleName;
        static bool s_removedStaleModules = false;
        if (!s_removedStaleModules)
        {
            RemoveStaleModuleCopies(modulePath.parent_path());
            // A reload snapshot and its flag can only be written later in this process, by the reload
            // cycle itself. Anything already on disk at the first load is left over from a killed or
            // crashed run: drop it so a cold start keeps the normal splash + last_scene path instead of
            // silently restoring stale state, and so a snapshot that kills the restore cannot do it again
            // on every launch.
            std::error_code stale;
            std::filesystem::remove(pe::Path::Executable + "reload_state.json", stale);
            std::filesystem::remove(pe::Path::Executable + "reload.flag", stale);
            s_removedStaleModules = true;
        }
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
#elif defined(PE_WIN32)
        // The module copy can transiently fail with ACCESS_DENIED/SHARING_VIOLATION when an anti-virus
        // (e.g. Bitdefender) is scanning the freshly-built DLL, or when a stale versioned copy from a
        // previous/force-killed run is still locked. Probe a fresh versioned name on each try and retry
        // briefly so the scan/lock can clear instead of failing the whole launch on the first attempt.
        static int s_gen = 0;
        const std::string srcPath = modulePath.string();
        constexpr int kMaxCopyAttempts = 50; // ~5s of 100ms retries — outlasts a typical AV scan
        std::filesystem::path versionedPath;
        for (int attempt = 0; attempt < kMaxCopyAttempts; ++attempt)
        {
            char versioned[256];
            std::snprintf(versioned, sizeof(versioned), "PhasmaEditorModule_%04d.dll", s_gen++);
            std::filesystem::path candidate = std::filesystem::path(pe::Path::Executable) / versioned;

            std::error_code ec;
            std::filesystem::remove(candidate, ec); // clear a removable stale copy at this name

            if (CopyFileA(srcPath.c_str(), candidate.string().c_str(), FALSE))
            {
                versionedPath = std::move(candidate);
                break;
            }

            const DWORD err = ::GetLastError();
            std::filesystem::remove(candidate, ec); // don't leave a partial copy behind
            if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION)
            {
                PE_ERROR("CopyFileA failed: %lu", err);
                return m;
            }
            if (attempt + 1 < kMaxCopyAttempts)
                ::Sleep(100); // transient AV scan / file lock — wait, then try a fresh name
        }
        if (versionedPath.empty())
        {
            PE_ERROR("CopyFileA failed: module '%s' stayed locked after %d attempts "
                     "(anti-virus scan or a running editor instance?)",
                     k_moduleName, kMaxCopyAttempts);
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

    void UnloadModule(ModuleHandle &m, ModuleUnloadMode mode = ModuleUnloadMode::Reload)
    {
        if (!m.lib)
            return;
#if defined(PE_WIN32) && defined(PE_TRACY)
        (void)mode;
        // Explicit FreeLibrary runs Tracy's DLL-local static profiler destructor and can
        // hang while it joins Tracy's worker thread. Keep copied modules loaded in Tracy
        // Windows builds; versioned DLL names avoid source-module locks, and stale copies
        // are cleaned on the next startup after process exit.
        m = {};
        return;
#endif
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

        UnloadModule(mod, ModuleUnloadMode::ProcessExit);
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
