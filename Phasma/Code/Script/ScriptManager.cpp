#include "ScriptManager.h"
#include "Script.h"

namespace pe
{
    void ScriptManager::Init()
    {
        // Configure CMake
        std::string cmakeCommand = "cmake -S " + Path::Executable + "Assets/Scripts -B " + Path::Executable + "Assets/Scripts/Build";
        if (std::system(cmakeCommand.c_str()) != 0)
            PE_ERROR("Failed to configure CMake");

// Compile the source into a shared library
#ifdef PE_DEBUG
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/Build --config Debug";
#elif defined(PE_RELEASE)
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/Build --config Release";
#elif defined(PE_MINSIZEREL)
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/Build --config MinSizeRel";
#elif defined(PE_RELWITHDEBINFO)
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/Build --config RelWithDebInfo";
#endif
        if (std::system(buildCommand.c_str()) != 0)
            PE_ERROR("Failed to build the scripts");

        // Load the compiled module
        LoadModule();

        // Find .cpp files in the Scripts directory
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Executable + "Assets/Scripts"))
        {
            if (file.path().extension() == ".cpp")
            {
                std::string filePath = file.path().string();
                std::replace(filePath.begin(), filePath.end(), '\\', '/');

                // Create a new script object
                Script *script = new Script(filePath);
                script->CreateObject();
                m_scripts.push_back(script);
            }
        }
    }

    void ScriptManager::Shutdown()
    {
        for (auto script : m_scripts)
        {
            if (script)
                delete script;
        }
        m_scripts.clear();

        UnloadModule();
    }

    void ScriptManager::LoadModule()
    {
        s_module = nullptr;
        std::string path;

#if defined(PE_WIN32)
#ifdef PE_DEBUG
        path = Path::Executable + "Assets/Scripts/Build/Debug/Scripts.dll";
#elif defined(PE_RELEASE)
        path = Path::Executable + "Assets/Scripts/Build/Release/Scripts.dll";
#elif defined(PE_MINSIZEREL)
        path = Path::Executable + "Assets/Scripts/Build/MinSizeRel/Scripts.dll";
#elif defined(PE_RELWITHDEBINFO)
        path = Path::Executable + "Assets/Scripts/Build/RelWithDebInfo/Scripts.dll";
#endif
        std::wstring widePath(path.begin(), path.end());
        s_module = (void *)LoadLibrary(widePath.c_str());
#else
        path = Path::Executable + "Assets/Scripts/Build/libScripts.so";
        //  m_module = dlopen(path.c_str(), RTLD_NOW);
        s_module = dlopen(path.c_str(), RTLD_LAZY);
#endif

        PE_ERROR_IF(!s_module, "Failed to load module: " + path);
    }

    void ScriptManager::UnloadModule()
    {
#if defined(PE_WIN32)
        FreeLibrary((HMODULE)s_module);
#else
        dlclose(s_module);
#endif
        s_module = nullptr;
    }
}
