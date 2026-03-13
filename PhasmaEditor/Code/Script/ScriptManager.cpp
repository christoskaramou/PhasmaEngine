#include "ScriptManager.h"

namespace pe
{
    void ScriptManager::Init()
    {
        s_lua.open_libraries(
            sol::lib::base,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::io,
            sol::lib::os,
            sol::lib::coroutine);

        RegisterBindings();
        LoadScripts();

        // Call init() on each loaded script
        sol::protected_function initFn = s_lua["init"];
        if (initFn.valid())
        {
            auto result = initFn();
            if (!result.valid())
            {
                sol::error err = result;
                PE_ERROR("Lua init() error: %s", err.what());
            }
        }

        m_initialized = true;
    }

    void ScriptManager::Update()
    {
        sol::protected_function updateFn = s_lua["update"];
        if (updateFn.valid())
        {
            auto result = updateFn();
            if (!result.valid())
            {
                sol::error err = result;
                PE_ERROR("Lua update() error: %s", err.what());
            }
        }
    }

    void ScriptManager::Draw()
    {
        sol::protected_function drawFn = s_lua["draw"];
        if (drawFn.valid())
        {
            auto result = drawFn();
            if (!result.valid())
            {
                sol::error err = result;
                PE_ERROR("Lua draw() error: %s", err.what());
            }
        }
    }

    void ScriptManager::Destroy()
    {
        if (!m_initialized)
            return;

        sol::protected_function destroyFn = s_lua["destroy"];
        if (destroyFn.valid())
        {
            auto result = destroyFn();
            if (!result.valid())
            {
                sol::error err = result;
                PE_ERROR("Lua destroy() error: %s", err.what());
            }
        }

        m_scriptPaths.clear();
        s_lua = sol::state(); // Reset state
        m_initialized = false;
    }

    void ScriptManager::Reload()
    {
        Destroy();
        Init();
    }

    void ScriptManager::LoadScripts()
    {
        const std::string scriptsDir = Path::Assets + "Scripts";

        if (!std::filesystem::exists(scriptsDir))
            return;

        for (auto &file : std::filesystem::recursive_directory_iterator(scriptsDir))
        {
            if (file.path().extension() == ".lua")
            {
                std::string filePath = file.path().string();
                std::replace(filePath.begin(), filePath.end(), '\\', '/');

                auto result = s_lua.safe_script_file(filePath, sol::script_pass_on_error);
                if (!result.valid())
                {
                    sol::error err = result;
                    PE_ERROR("Lua script error in '%s': %s", filePath.c_str(), err.what());
                    continue;
                }

                m_scriptPaths.push_back(filePath);
                PE_INFO("Loaded Lua script: %s", filePath.c_str());
            }
        }
    }

    void ScriptManager::RegisterBindings()
    {
        // Engine bindings will be registered here
        // e.g. s_lua.set_function("log", [](const std::string &msg) { PE_INFO("%s", msg.c_str()); });
        s_lua.set_function("pe_log", [](const std::string &msg)
                           { PE_INFO("[Lua] %s", msg.c_str()); });

        s_lua.set_function("pe_warn", [](const std::string &msg)
                           { PE_WARN("[Lua] %s", msg.c_str()); });

        s_lua.set_function("pe_error", [](const std::string &msg)
                           { PE_ERROR("[Lua] %s", msg.c_str()); });
    }
} // namespace pe
