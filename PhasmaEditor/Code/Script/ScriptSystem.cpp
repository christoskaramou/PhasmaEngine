#if defined(PE_SCRIPTS)
#include "ScriptSystem.h"
#include "GUI/GUIState.h"
#include "Scene/Model.h"
#include "Systems/RendererSystem.h"
#include "Base/FileWatcher.h"
#include "Base/Timer.h"

namespace pe
{
    Image *LuaImage::Get()
    {
        return ptr;
    }

    void ScriptSystem::Init(CommandBuffer *cmd)
    {
        InitInternal(cmd, false);
    }

    void ScriptSystem::InitRestricted(CommandBuffer *cmd)
    {
        InitInternal(cmd, true);
    }

    void ScriptSystem::InitInternal(CommandBuffer *cmd, bool restricted)
    {
        if (restricted)
        {
            m_lua.open_libraries(
                sol::lib::base,
                sol::lib::math,
                sol::lib::string,
                sol::lib::table,
                sol::lib::coroutine);
        }
        else
        {
            m_lua.open_libraries(
                sol::lib::base,
                sol::lib::math,
                sol::lib::string,
                sol::lib::table,
                sol::lib::io,
                sol::lib::os,
                sol::lib::coroutine);
        }

        // Logging
        m_lua.set_function("pe_log", [](const std::string &msg)
                           { PE_INFO("[Lua] %s", msg.c_str()); });
        m_lua.set_function("pe_warn", [](const std::string &msg)
                           { PE_WARN("[Lua] %s", msg.c_str()); });
        m_lua.set_function("pe_error", [](const std::string &msg)
                           { Log::Error("[Lua] " + msg); });

        // hooks {} keyword - lets scripts register lifecycle hooks from local functions
        m_lua.set_function("hooks", [](sol::table t, sol::this_environment te)
                           {
            sol::environment env = te;
            env.raw_set("__hooks", t); });

        // Execute all registered binding functions
        for (auto &fn : GetBindings())
            fn(m_lua);

        if (!restricted)
            LoadScripts();

        m_initialized = true;
    }

    void ScriptSystem::CollectHooks(ScriptEntry &entry)
    {
        // If the script used hooks{}, read from that table; otherwise fall back to env
        sol::object hooksObj = entry.env.raw_get<sol::object>("__hooks");
        bool hasHooksTable = hooksObj.is<sol::table>();
        sol::table hooksTable = hasHooksTable ? hooksObj.as<sol::table>() : sol::table{};

        auto get = [&](const char *name) -> sol::function
        {
            sol::object obj = hasHooksTable ? hooksTable[name] : entry.env[name];
            return obj.is<sol::function>() ? obj.as<sol::function>() : sol::function{};
        };

        entry.initFn = get("init");
        entry.updateFn = get("update");
        entry.updateEditorFn = get("update_editor");
        entry.destroyFn = get("destroy");
    }

    void ScriptSystem::CallInit()
    {
        for (auto &script : m_scripts)
        {
            if (script.initFn.valid())
            {
                auto result = script.initFn();
                if (!result.valid())
                {
                    sol::error err = result;
                    PE_ERROR("[Lua] init() error in '%s': %s", script.path.c_str(), err.what());
                }
            }
        }
    }

    void ScriptSystem::AddPendingAsyncLoad(PendingAsyncLoad load)
    {
        m_pendingAsyncLoads.push_back(std::move(load));
    }

    void ScriptSystem::ProcessAsyncLoads()
    {
        // First pass: collect all ready models
        struct CompletedLoad
        {
            Model *model;
            sol::function callback;
            std::shared_ptr<BatchLoadState> batchState;
            sol::function batchCallback;
        };
        std::vector<CompletedLoad> completed;

        for (auto it = m_pendingAsyncLoads.begin(); it != m_pendingAsyncLoads.end();)
        {
            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            completed.push_back({it->future.get(), it->callback, it->batchState, it->batchCallback});
            it = m_pendingAsyncLoads.erase(it);
        }

        if (completed.empty())
            return;

        // Add all ready models to scene in one batch
        auto *r = GetGlobalSystem<RendererSystem>();
        if (r)
        {
            r->WaitAllFramesCommands();
            for (auto &c : completed)
            {
                if (c.model)
                {
                    r->GetScene().AddModel(c.model);
                    c.model->SetRenderReady(true);
                }
            }
            r->GetScene().UpdateGeometryBuffers();
        }

        // Fire callbacks
        for (auto &c : completed)
        {
            if (c.callback.valid())
            {
                auto res = c.callback(c.model);
                if (!res.valid())
                {
                    sol::error err = res;
                    PE_ERROR("[Lua] async callback error: %s", err.what());
                }
            }

            if (c.batchState)
            {
                auto &state = c.batchState;
                if (c.model)
                    state->models.push_back(c.model);
                state->completed++;

                if (state->completed == state->total && c.batchCallback.valid())
                {
                    sol::table result = m_lua.create_table();
                    for (size_t i = 0; i < state->models.size(); i++)
                        result[i + 1] = state->models[i];

                    auto res = c.batchCallback(result);
                    if (!res.valid())
                    {
                        sol::error err = res;
                        PE_ERROR("[Lua] batch load callback error: %s", err.what());
                    }
                }
            }
        }
    }

    void ScriptSystem::Update()
    {
        // Process completed async model loads
        ProcessAsyncLoads();

        // Periodically scan for new .lua files
        ScanForNewScripts();

        // update_editor() runs every frame regardless of play mode
        for (auto &script : m_scripts)
        {
            if (script.updateEditorFn.valid())
            {
                auto result = script.updateEditorFn();
                if (!result.valid())
                {
                    sol::error err = result;
                    PE_ERROR("[Lua] update_editor() error in '%s': %s", script.path.c_str(), err.what());
                }
            }
        }

        if (!GUIState::s_playMode || GUIState::s_isPaused)
            return;

        // update() runs only in play mode
        for (auto &script : m_scripts)
        {
            if (script.updateFn.valid())
            {
                auto result = script.updateFn();
                if (!result.valid())
                {
                    sol::error err = result;
                    PE_ERROR("[Lua] update() error in '%s': %s", script.path.c_str(), err.what());
                }
            }
        }
    }

    void ScriptSystem::Destroy()
    {
        if (!m_initialized)
            return;

        {
            for (auto &script : m_scripts)
            {
                if (script.destroyFn.valid())
                {
                    auto result = script.destroyFn();
                    if (!result.valid())
                    {
                        sol::error err = result;
                        PE_ERROR("[Lua] destroy() error in '%s': %s", script.path.c_str(), err.what());
                    }
                }
            }
        }

        // Wait for any pending async loads before destroying Lua state
        for (auto &load : m_pendingAsyncLoads)
            load.future.wait();
        m_pendingAsyncLoads.clear();

        m_scripts.clear();
        m_lua = sol::state();
        m_initialized = false;
    }

    void ScriptSystem::Reload()
    {
        Destroy();
        Init(nullptr);
        CallInit();
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

                // Each script gets its own environment that inherits globals
                sol::environment env(m_lua, sol::create, m_lua.globals());

                auto result = m_lua.safe_script_file(filePath, env, sol::script_pass_on_error);
                if (!result.valid())
                {
                    sol::error err = result;
                    PE_ERROR("[Lua] script error in '%s': %s", filePath.c_str(), err.what());
                    continue;
                }

                // Promote non-hook globals so other scripts can access them
                // (e.g. main.lua calling run_buffer_tests(), or T table from test_utils)
                static const std::set<std::string> hookNames = {"init", "update", "update_editor", "destroy"};
                for (auto &[key, val] : env)
                {
                    if (key.is<std::string>())
                    {
                        std::string name = key.as<std::string>();
                        if (hookNames.find(name) == hookNames.end())
                        {
                            sol::object existing = m_lua.globals().raw_get<sol::object>(name);
                            if (existing.valid() && existing.get_type() != sol::type::lua_nil)
                                PE_WARN("[Lua] global '%s' redefined by '%s'", name.c_str(), filePath.c_str());
                            m_lua.globals()[name] = val;
                        }
                    }
                }

                ScriptEntry entry;
                entry.path = filePath;
                entry.env = std::move(env);
                CollectHooks(entry);
                m_scripts.push_back(std::move(entry));

                PE_INFO("Loaded Lua script: %s", filePath.c_str());
            }
        }
    }

    void ScriptSystem::ScanForNewScripts()
    {
        // Scan every 2 seconds
        double dt = FrameTimer::Instance().GetDelta();
        m_scanTimer += dt;
        if (m_scanTimer < 2.0)
            return;
        m_scanTimer = 0.0;

        const std::string scriptsDir = Path::Assets + "Scripts";
        if (!std::filesystem::exists(scriptsDir))
            return;

        bool foundNew = false;
        for (auto &file : std::filesystem::recursive_directory_iterator(scriptsDir))
        {
            if (file.path().extension() != ".lua")
                continue;

            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');

            // Check if already tracked
            bool known = false;
            for (auto &existing : m_scripts)
            {
                if (existing.path == filePath)
                {
                    known = true;
                    break;
                }
            }

            if (!known)
            {
                // Register with FileWatcher for future change detection
                FileWatcher::Add(filePath, [](size_t fileEvent)
                                 {
                    EventSystem::PushEvent(fileEvent);
                    EventSystem::PushEvent(EventType::CompileScripts); });

                foundNew = true;
                PE_INFO("New Lua script detected: %s", filePath.c_str());
            }
        }

        if (foundNew)
            Reload();
    }
} // namespace pe
#endif
