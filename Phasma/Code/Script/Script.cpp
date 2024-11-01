
#include "Script.h"
#include "Renderer/RHI.h"

namespace pe
{
    Script::Script(const std::string &path) : m_path{path}, m_hash{StringHash(path)}
    {
    }

    void ScriptManager::Init()
    {
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Scripts"))
        {
            std::string path = file.path().string();
            std::replace(path.begin(), path.end(), '\\', '/');

            Script script(path);
            s_scripts.emplace(script.m_hash, std::move(script));
        }
    }

    void ScriptManager::CompileScript(const Script &script)
    {
    }

    void ScriptManager::LoadScript(const Script &script)
    {
    }

    void ScriptManager::UnloadScript(const Script &script)
    {
    }

    void ScriptManager::UnloadAllScripts()
    {
    }

    void ScriptManager::PollScripts()
    {
        RHII.WaitDeviceIdle();
    }
}
