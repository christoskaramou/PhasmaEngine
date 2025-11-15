#include "ScriptManager.h"
#include "Script.h"

namespace pe
{
    void ScriptManager::Init()
    {
        // Configure CMake
        std::string cmakeCommand = "cmake -S " + Path::Executable + "Assets/Scripts -B " + Path::Executable + "Assets/Scripts/Build";
        int res = std::system(cmakeCommand.c_str());
        PE_ERROR_IF(res != 0, "Failed to configure CMake");

// Compile the source into a shared library
#ifdef PE_DEBUG
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/build --config Debug";
#elif defined(PE_RELEASE)
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/build --config Release";
#elif defined(PE_MINSIZEREL)
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/build --config MinSizeRel";
#elif defined(PE_RELWITHDEBINFO)
        std::string buildCommand = "cmake --build " + Path::Executable + "Assets/Scripts/build --config RelWithDebInfo";
#endif
        res = std::system(buildCommand.c_str());
        PE_ERROR_IF(res != 0, "Failed to build the scripts");

        // Load the compiled module
        LoadModule();

        // Find .cpp files in the Scripts directory
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Executable + "Assets/Scripts"))
        {
            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');
            
            if (filePath.find("Assets/Scripts/Build") != std::string::npos)
                continue;

            if (file.path().extension() == ".cpp")
            {
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
        path = Path::Executable + "Assets/Scripts/build/Debug/Scripts.dll";
#elif defined(PE_RELEASE)
        path = Path::Executable + "Assets/Scripts/build/Release/Scripts.dll";
#elif defined(PE_MINSIZEREL)
        path = Path::Executable + "Assets/Scripts/build/MinSizeRel/Scripts.dll";
#elif defined(PE_RELWITHDEBINFO)
        path = Path::Executable + "Assets/Scripts/build/RelWithDebInfo/Scripts.dll";
#endif
        std::wstring widePath(path.begin(), path.end());
        s_module = (void *)LoadLibrary(widePath.c_str());
#else
        path = Path::Executable + "Assets/Scripts/build/libScripts.so";
        //  m_module = dlopen(path.c_str(), RTLD_NOW);
        s_module = dlopen(path.c_str(), RTLD_LAZY);
#endif

        PE_ERROR_IF(!s_module, std::string("Failed to load module: " + path).c_str());
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
