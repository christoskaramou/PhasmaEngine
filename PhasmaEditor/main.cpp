#include "Base/Log.h"
#include "Base/EventSystem.h"
#include "Base/ThreadPool.h"

#if defined(PE_LINUX)
#include <dlfcn.h>
static constexpr const char *k_moduleName = "libPhasmaEditorModule.so";
#elif defined(PE_WIN32)
static constexpr const char *k_moduleName = "PhasmaEditorModule.dll";
#endif

using TickFunc = bool (*)();
using DestroyFunc = void (*)();

namespace
{
    struct ModuleHandle
    {
        void *lib = nullptr;
        TickFunc tick = nullptr;
        DestroyFunc destroy = nullptr;
    };

    ModuleHandle LoadModule()
    {
        ModuleHandle m;
#if defined(PE_LINUX)
        m.lib = dlopen(k_moduleName, RTLD_NOW | RTLD_LOCAL);
        if (!m.lib)
        {
            PE_ERROR("dlopen failed: %s", dlerror());
            return m;
        }
        m.tick = reinterpret_cast<TickFunc>(dlsym(m.lib, "TickEditorModule"));
        m.destroy = reinterpret_cast<DestroyFunc>(dlsym(m.lib, "DestroyEditorModule"));
#elif defined(PE_WIN32)
        static int s_gen = 0;
        char versioned[256];
        std::snprintf(versioned, sizeof(versioned), "PhasmaEditorModule_%04d.dll", s_gen++);
        CopyFileA(k_moduleName, versioned, FALSE);
        m.lib = static_cast<void *>(::LoadLibraryA(versioned));
        if (!m.lib)
        {
            PE_ERROR("LoadLibraryA failed: %lu", ::GetLastError());
            return m;
        }
        m.tick =
            reinterpret_cast<TickFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "TickEditorModule"));
        m.destroy =
            reinterpret_cast<DestroyFunc>(::GetProcAddress(static_cast<HMODULE>(m.lib), "DestroyEditorModule"));
#endif
        if (!m.tick || !m.destroy)
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
        m = {};
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();

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
    return 0;
}
