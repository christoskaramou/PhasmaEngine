#include "ScriptSystem.h"
#include "GUI/GUIState.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetAssimp.h"
#include "Scene/Scene.h"
#include "Scene/SceneNodeHandle.h"
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

        // exposed {} keyword - declares variables to show in the editor's Properties panel.
        // Returns the same table so scripts can hold a live reference:
        //   local props = exposed { speed = 5.0, enabled = true }
        //   -- props.speed always reflects the current editor value
        m_lua.set_function("exposed", [](sol::table t, sol::this_environment te) -> sol::table
                           {
            sol::environment env = te;
            env.raw_set("__exposed", t);
            return t; });

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

    void ScriptSystem::CollectExposedVars(ScriptEntry &entry)
    {
        entry.exposedVars.clear();

        sol::object exposedObj = entry.env.raw_get<sol::object>("__exposed");
        if (!exposedObj.is<sol::table>())
            return;

        sol::table t = exposedObj.as<sol::table>();
        for (auto &[k, v] : t)
        {
            if (!k.is<std::string>())
                continue;

            ExposedVar var;
            var.name = k.as<std::string>();

            // Check bool before number: in sol2 booleans also satisfy is<double>()
            if (v.is<bool>())
                var.type = ExposedVar::Type::Bool;
            else if (v.is<double>())
                var.type = ExposedVar::Type::Number;
            else if (v.is<std::string>())
                var.type = ExposedVar::Type::String;
            else
                continue; // skip unsupported types

            entry.exposedVars.push_back(std::move(var));
        }
    }

    static std::string NormalizePath(const std::string &path)
    {
        // Resolve '..' and normalize separators so paths stored by LoadScripts
        // (which may include '..') match paths returned by the FileSelector.
        try
        {
            auto canonical = std::filesystem::weakly_canonical(path);
            std::string s = canonical.string();
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }
        catch (...)
        {
            std::string s = path;
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }
    }

    ScriptEntry *ScriptSystem::FindScript(const std::string &path)
    {
        // entry.path was already normalized by LoadScripts; only normalize the input
        std::string normalized = NormalizePath(path);
        for (auto &entry : m_scripts)
        {
            if (entry.path == normalized)
                return &entry;
        }
        return nullptr;
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

    void ScriptSystem::AddPendingSceneLoad(PendingSceneLoad load)
    {
        m_pendingSceneLoads.push_back(std::move(load));
    }

    void ScriptSystem::ProcessAsyncLoads()
    {
        struct CompletedLoad
        {
            ModelAsset *model;
            SceneNodeHandle handle;
            sol::function callback;
            std::shared_ptr<BatchLoadState> batchState;
            sol::function batchCallback;
            uint32_t sceneGeneration;
        };
        std::vector<CompletedLoad> completed;

        for (auto it = m_pendingAsyncLoads.begin(); it != m_pendingAsyncLoads.end();)
        {
            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            CompletedLoad c;
            c.model = it->future.get();
            c.callback = it->callback;
            c.batchState = it->batchState;
            c.batchCallback = it->batchCallback;
            c.sceneGeneration = it->sceneGeneration;
            completed.push_back(std::move(c));
            it = m_pendingAsyncLoads.erase(it);
        }

        if (completed.empty())
            return;

        auto *r = GetGlobalSystem<RendererSystem>();
        if (!r)
            return;

        Scene &scene = r->GetScene();

        for (auto &c : completed)
        {
            if (!c.model)
                continue;

            // Discard if scene changed since request
            if (c.sceneGeneration != scene.GetGeneration())
            {
                delete c.model;
                c.model = nullptr;
                continue;
            }

            // GPU upload on main thread (textures were only decoded on the background thread)
            if (auto *assimp = dynamic_cast<ModelAssetAssimp *>(c.model))
                assimp->UploadGpu();

            c.handle = scene.AddModelDeferred(c.model);
        }

        // Callbacks
        for (auto &c : completed)
        {
            if (c.callback.valid())
            {
                sol::protected_function_result res;
                if (c.model)
                    res = c.callback(c.handle, sol::nil);
                else
                    res = c.callback(sol::nil, std::string("load failed or scene changed"));

                if (!res.valid())
                {
                    sol::error err = res;
                    PE_ERROR("[Lua] async callback error: %s", err.what());
                }
            }

            if (c.batchState)
            {
                if (c.handle.nodeId)
                    c.batchState->models.push_back(c.handle);
                c.batchState->completed++;

                if (c.batchState->completed == c.batchState->total && c.batchCallback.valid())
                {
                    // Pass the models vector directly — sol::as_table wraps it with proper usertype info
                    auto res = c.batchCallback(sol::as_table(c.batchState->models));
                    if (!res.valid())
                    {
                        sol::error err = res;
                        PE_ERROR("[Lua] batch load callback error: %s", err.what());
                    }
                }
            }
        }

        if (m_pendingAsyncLoads.empty())
            GUIState::s_modelLoading = false;
    }

    void ScriptSystem::ProcessSceneLoads()
    {
        if (m_pendingSceneLoads.empty())
            return;

        auto *r = GetGlobalSystem<RendererSystem>();
        if (!r)
            return;

        for (auto it = m_pendingSceneLoads.begin(); it != m_pendingSceneLoads.end();)
        {
            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            std::unique_ptr<Scene::ScenePreload> preload(it->future.get());
            Scene &scene = r->GetScene();

            if (it->sceneGeneration != scene.GetGeneration())
            {
                // Scene changed since request — discard
                if (it->callback.valid())
                {
                    auto res = it->callback(std::string("scene changed during load"));
                    if (!res.valid())
                    {
                        sol::error err = res;
                        PE_ERROR("[Lua] scene load callback error: %s", err.what());
                    }
                }
                it = m_pendingSceneLoads.erase(it);
                continue;
            }

            r->WaitAllFramesCommands();
            scene.LoadSceneApply(std::move(*preload));

            GUIState::s_modelLoading = false;

            if (it->callback.valid())
            {
                auto res = it->callback(sol::nil); // nil = no error
                if (!res.valid())
                {
                    sol::error err = res;
                    PE_ERROR("[Lua] scene load callback error: %s", err.what());
                }
            }

            it = m_pendingSceneLoads.erase(it);
        }
    }

    void ScriptSystem::Update()
    {
        PE_PROFILE_SCOPE("Script System");
        // Process completed async model loads
        ProcessAsyncLoads();
        // Process completed async scene loads
        ProcessSceneLoads();

        // Periodically scan for new .lua files
        ScanForNewScripts();

        // update_editor() runs every frame when not in play mode,
        // so scripts can have live editor functionality without entering play mode
        if (!GUIState::s_playMode)
        {
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
        }

        if (GUIState::s_playMode && !GUIState::s_isPaused)
        {
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

        // Wait for any pending async scene loads
        for (auto &load : m_pendingSceneLoads)
        {
            if (load.future.valid())
            {
                Scene::ScenePreload *p = load.future.get();
                delete p;
            }
        }
        m_pendingSceneLoads.clear();

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
                // Store canonical path so FindScript can match regardless of whether
                // Path::Assets contains '..' segments vs FileSelector canonical paths
                std::string filePath = NormalizePath(file.path().string());

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
                CollectExposedVars(entry);
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

            std::string filePath = NormalizePath(file.path().string());

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
