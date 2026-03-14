#if defined(PE_SCRIPTS)
#include "ScriptSystem.h"
#include "GUI/GUIState.h"

namespace pe
{
    Image *LuaImage::Get()
    {
        return ptr;
    }

    void ScriptSystem::Init(CommandBuffer *cmd)
    {
        m_lua.open_libraries(
            sol::lib::base,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::io,
            sol::lib::os,
            sol::lib::coroutine);

        // Logging
        m_lua.set_function("pe_log", [](const std::string &msg)
                           { PE_INFO("[Lua] %s", msg.c_str()); });
        m_lua.set_function("pe_warn", [](const std::string &msg)
                           { PE_WARN("[Lua] %s", msg.c_str()); });
        m_lua.set_function("pe_error", [](const std::string &msg)
                           { Log::Error("[Lua] " + msg); });

        // Execute all registered binding functions
        for (auto &fn : GetBindings())
            fn(m_lua);

        LoadScripts();

        sol::protected_function initFn = m_lua["init"];
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

    void ScriptSystem::Update()
    {
        if (!GUIState::s_playMode || GUIState::s_isPaused)
            return;

        sol::protected_function updateFn = m_lua["update"];
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

    void ScriptSystem::Destroy()
    {
        if (!m_initialized)
            return;

        {
            sol::protected_function destroyFn = m_lua["destroy"];
            if (destroyFn.valid())
            {
                auto result = destroyFn();
                if (!result.valid())
                {
                    sol::error err = result;
                    PE_ERROR("Lua destroy() error: %s", err.what());
                }
            }
        } // destroyFn released before Lua state reset

        m_scriptPaths.clear();
        m_lua = sol::state();
        m_initialized = false;
    }

    void ScriptSystem::Reload()
    {
        Destroy();
        Init(nullptr);
    }

    void ScriptSystem::AddBindings(LuaBindingFunc func)
    {
        GetBindings().push_back(std::move(func));
    }

    std::vector<LuaBindingFunc> &ScriptSystem::GetBindings()
    {
        static std::vector<LuaBindingFunc> bindings;
        return bindings;
    }

    std::string ScriptSystem::ExecuteLua(const std::string &code)
    {
        if (!m_initialized)
            return "error: ScriptSystem not initialized";

        // Temporarily redirect pe_log output to capture buffer
        std::string captured;
        sol::function originalLog = m_lua["pe_log"];

        m_lua.set_function("pe_log", [&captured](const std::string &msg)
                           {
            if (!captured.empty()) captured += "\n";
            captured += msg; });

        auto result = m_lua.safe_script(code, sol::script_pass_on_error);

        // Restore original pe_log
        m_lua["pe_log"] = originalLog;

        if (!result.valid())
        {
            sol::error err = result;
            return "error: " + std::string(err.what());
        }

        // Append return value if any
        if (result.get_type() != sol::type::none && result.get_type() != sol::type::nil)
        {
            std::string retVal;
            try
            {
                sol::object obj = result;
                if (obj.is<std::string>())
                    retVal = obj.as<std::string>();
                else if (obj.is<double>())
                    retVal = std::to_string(obj.as<double>());
                else if (obj.is<bool>())
                    retVal = obj.as<bool>() ? "true" : "false";
                else
                    retVal = "(value)";
            }
            catch (...)
            {
                retVal = "(value)";
            }

            if (!captured.empty())
                captured += "\nreturn: " + retVal;
            else
                captured = retVal;
        }

        if (captured.empty())
            captured = "ok";

        return captured;
    }

    void ScriptSystem::LoadScripts()
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

                auto result = m_lua.safe_script_file(filePath, sol::script_pass_on_error);
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
} // namespace pe
#endif
