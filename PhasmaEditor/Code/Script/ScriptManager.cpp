#include "ScriptManager.h"
#include "Script.h"

#include <vector>

#if defined(PE_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    int RunProcess(const std::vector<std::string> &args)
    {
        if (args.empty())
            return -1;

#if defined(PE_WIN32)
        std::vector<const char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        intptr_t result = _spawnvp(_P_WAIT, args[0].c_str(), argv.data());
        return result == -1 ? -1 : static_cast<int>(result);
#else
        pid_t pid = fork();
        if (pid == 0)
        {
            std::vector<char *> argv;
            argv.reserve(args.size() + 1);
            for (const auto &arg : args)
                argv.push_back(const_cast<char *>(arg.c_str()));
            argv.push_back(nullptr);

            execvp(args[0].c_str(), argv.data());
            _exit(127);
        }
        if (pid < 0)
            return -1;

        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
            return -1;

        if (WIFEXITED(status))
            return WEXITSTATUS(status);

        return -1;
#endif
    }
}

namespace pe
{
    void ScriptManager::Init()
    {
        const std::string scriptsDir = Path::Assets + "Scripts";
        const std::string buildDir = Path::Assets + "Scripts/build";

        // Configure CMake
        std::vector<std::string> cmakeArgs = {
            "cmake",
            "-S",
            scriptsDir,
            "-B",
            buildDir};
        int res = RunProcess(cmakeArgs);
        PE_ERROR_IF(res != 0, "Failed to configure CMake");

        // Compile the source into a shared library
        std::vector<std::string> buildArgs = {
            "cmake",
            "--build",
            buildDir,
            "--config"};
#if defined(PE_DEBUG)
        buildArgs.push_back("Debug");
#elif defined(PE_RELEASE)
        buildArgs.push_back("Release");
#elif defined(PE_MINSIZEREL)
        buildArgs.push_back("MinSizeRel");
#elif defined(PE_RELWITHDEBINFO)
        buildArgs.push_back("RelWithDebInfo");
#endif
        res = RunProcess(buildArgs);
        PE_ERROR_IF(res != 0, "Failed to build the scripts");

        // Load the compiled module
        LoadModule();

        // Find .pecpp files in Scripts folder
        for (auto &file : std::filesystem::recursive_directory_iterator(scriptsDir))
        {
            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');

            if (filePath.find("Assets/Scripts/build") != std::string::npos)
                continue;

            if (file.path().extension() == ".pecpp")
            {
                // Create a new script object
                Script *script = new Script(filePath);
                script->CreateObject();
                m_scripts.push_back(script);
            }
        }
    }

    void ScriptManager::Update()
    {
        std::vector<std::future<void>> updateFutures(m_scripts.size());
        for (size_t i = 0; i < m_scripts.size(); i++)
        {
            if (m_scripts[i])
                updateFutures[i] = std::async(std::launch::async, &Script::Update, m_scripts[i]);
        }

        for (auto &future : updateFutures)
        {
            if (future.valid())
                future.wait();
        }
    }

    void ScriptManager::Draw()
    {
        std::vector<std::future<void>> drawFutures(m_scripts.size());
        for (size_t i = 0; i < m_scripts.size(); i++)
        {
            if (m_scripts[i])
                drawFutures[i] = std::async(std::launch::async, &Script::Draw, m_scripts[i]);
        }

        for (auto &future : drawFutures)
        {
            if (future.valid())
                future.wait();
        }
    }

    void ScriptManager::Destroy()
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
        path = Path::Assets + "Scripts/build/Debug/Scripts.dll";
#elif defined(PE_RELEASE)
        path = Path::Assets + "Scripts/build/Release/Scripts.dll";
#elif defined(PE_MINSIZEREL)
        path = Path::Assets + "Scripts/build/MinSizeRel/Scripts.dll";
#elif defined(PE_RELWITHDEBINFO)
        path = Path::Assets + "Scripts/build/RelWithDebInfo/Scripts.dll";
#endif
        std::wstring widePath(path.begin(), path.end());
        s_module = (void *)LoadLibrary(widePath.c_str());
#else
        path = Path::Assets + "Scripts/build/libScripts.so";
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
} // namespace pe
