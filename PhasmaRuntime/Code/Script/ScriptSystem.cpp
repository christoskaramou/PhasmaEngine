#include "ScriptSystem.h"
#include "Camera/Camera.h"
#include "Render/ScriptRenderPasses.h"
#include "Scene/ModelAsset.h"
#if defined(PE_ENABLE_ASSIMP)
#include "Scene/ModelAssetAssimp.h"
#endif
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/SceneHost.h"
#include "Script/ScriptRuntimeHooks.h"
#include "UI/RuntimeUi.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace pe
{
    namespace
    {
        constexpr const char *kEditorOnlyScriptMarker = "phasma: editor-only";

        static bool IsLuaIdentifier(const std::string &name)
        {
            if (name.empty())
                return false;

            const auto isIdentFirst = [](unsigned char c)
            {
                return std::isalpha(c) || c == '_';
            };
            const auto isIdent = [](unsigned char c)
            {
                return std::isalnum(c) || c == '_';
            };

            if (!isIdentFirst(static_cast<unsigned char>(name.front())))
                return false;

            for (char c : name)
            {
                if (!isIdent(static_cast<unsigned char>(c)))
                    return false;
            }

            return true;
        }

        static bool IsSkippedLuaGlobal(const std::string &name)
        {
            static constexpr const char *kSkippedGlobals[] = {
                "_G",
                "_VERSION",
                "arg",
                "assert",
                "collectgarbage",
                "coroutine",
                "debug",
                "dofile",
                "error",
                "getmetatable",
                "io",
                "ipairs",
                "load",
                "loadfile",
                "math",
                "next",
                "os",
                "package",
                "pairs",
                "pcall",
                "print",
                "rawequal",
                "rawget",
                "rawlen",
                "rawset",
                "require",
                "select",
                "setmetatable",
                "string",
                "table",
                "tonumber",
                "tostring",
                "type",
                "utf8",
                "xpcall",
            };

            for (const char *skipped : kSkippedGlobals)
            {
                if (name == skipped)
                    return true;
            }

            return false;
        }

        static bool ShouldSkipLuaFunctionKey(const std::string &key, bool topLevel)
        {
            if (key.empty() || key.front() == '_')
                return true;

            return topLevel && IsSkippedLuaGlobal(key);
        }

        static void CollectLuaFunctions(lua_State *L,
                                        int tableIndex,
                                        const std::string &prefix,
                                        int depth,
                                        std::vector<std::string> &functions,
                                        std::unordered_set<const void *> &visitedTables)
        {
            if (depth < 0)
                return;

            tableIndex = lua_absindex(L, tableIndex);
            const void *tablePtr = lua_topointer(L, tableIndex);
            if (!tablePtr || !visitedTables.insert(tablePtr).second)
                return;

            lua_pushnil(L);
            while (lua_next(L, tableIndex) != 0)
            {
                const int keyType = lua_type(L, -2);
                if (keyType != LUA_TSTRING)
                {
                    lua_pop(L, 1);
                    continue;
                }

                size_t keyLen = 0;
                const char *keyPtr = lua_tolstring(L, -2, &keyLen);
                std::string key(keyPtr ? keyPtr : "", keyLen);
                const bool topLevel = prefix.empty();
                if (ShouldSkipLuaFunctionKey(key, topLevel) || !IsLuaIdentifier(key))
                {
                    lua_pop(L, 1);
                    continue;
                }

                const std::string name = topLevel ? key : prefix + "." + key;
                const int valueType = lua_type(L, -1);
                if (valueType == LUA_TFUNCTION)
                    functions.push_back(name + "()");
                else if (valueType == LUA_TTABLE && depth > 0)
                    CollectLuaFunctions(L, -1, name, depth - 1, functions, visitedTables);

                lua_pop(L, 1);
            }
        }
    } // namespace

    static std::string NormalizeSlashes(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    static std::string NormalizePath(const std::string &path)
    {
        try
        {
            auto canonical = std::filesystem::weakly_canonical(path);
            return NormalizeSlashes(canonical.string());
        }
        catch (...)
        {
            return NormalizeSlashes(path);
        }
    }

    static bool MatchesNodeScriptPath(const std::string &currentPath, const NodeScriptInstance &inst)
    {
        if (currentPath.empty())
            return false;

        if (currentPath == inst.sourcePath || currentPath == inst.path)
            return true;

        const std::string normalized = NormalizeSlashes(currentPath);
        if (normalized == inst.sourcePath || normalized == inst.path)
            return true;

        return NormalizePath(currentPath) == inst.path;
    }

    static bool HasNodeInstanceForPath(const std::vector<NodeScriptInstance> &instances, const std::string &path)
    {
        for (const auto &inst : instances)
        {
            if (inst.path == path)
                return true;
        }
        return false;
    }

    static bool HasLoadedScriptPath(const std::vector<ScriptEntry> &scripts, const std::string &path)
    {
        for (const auto &script : scripts)
        {
            if (script.path == path)
                return true;
        }
        return false;
    }

    static std::string MakeScriptDir(const std::string &assetsRoot, const char *subdir)
    {
        if (assetsRoot.empty())
            return {};

        return (std::filesystem::path(assetsRoot) / "Scripts" / subdir).string();
    }

    static bool MeshBindingPayloadChanged(const Scene &scene, const NodeScriptInstance &inst, int meshRef)
    {
        if (meshRef != inst.bindingMeshRef)
            return true;
        if (meshRef < 0 || meshRef >= static_cast<int>(scene.GetMeshes().size()))
            return false;

        const Mesh &mesh = scene.GetMesh(meshRef);
        return mesh.vertexCount != inst.bindingMeshVertexCount ||
               mesh.indexCount != inst.bindingMeshIndexCount ||
               mesh.boundingBox.min != inst.bindingMeshBounds.min ||
               mesh.boundingBox.max != inst.bindingMeshBounds.max;
    }

    static void StoreMeshBindingPayload(const Scene &scene, NodeScriptInstance &inst, int meshRef)
    {
        inst.bindingMeshRef = meshRef;
        inst.bindingMeshVertexCount = 0;
        inst.bindingMeshIndexCount = 0;
        inst.bindingMeshBounds = {};

        if (meshRef < 0 || meshRef >= static_cast<int>(scene.GetMeshes().size()))
            return;

        const Mesh &mesh = scene.GetMesh(meshRef);
        inst.bindingMeshVertexCount = mesh.vertexCount;
        inst.bindingMeshIndexCount = mesh.indexCount;
        inst.bindingMeshBounds = mesh.boundingBox;
    }

    static bool IsSupportTestScriptPath(const std::string &path)
    {
        return path.ends_with("/tests/test_utils.lua");
    }

    static std::string MakeTestRunnerName(const std::string &path)
    {
        return "run_" + std::filesystem::path(path).stem().string();
    }

    static bool IsEditorOnlyGlobalScript(const std::filesystem::path &path)
    {
        std::ifstream file(path);
        if (!file)
            return false;

        std::string line;
        for (int i = 0; i < 16 && std::getline(file, line); ++i)
        {
            if (line.find(kEditorOnlyScriptMarker) != std::string::npos)
                return true;
        }

        return false;
    }

    static bool ShouldLoadGlobalScript(const std::filesystem::path &path, const std::string &filePath, bool logSkip)
    {
        if (!IsEditorOnlyGlobalScript(path))
            return true;

        if (ShouldLoadEditorOnlyGlobalScripts())
            return true;

        if (logSkip)
            PE_INFO("Skipped editor-only Lua script: %s", filePath.c_str());
        return false;
    }

    static ScriptUpdateMode ParseScriptUpdateMode(std::string mode)
    {
        std::transform(mode.begin(), mode.end(), mode.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (mode == "editor" || mode == "edit")
            return ScriptUpdateMode::Editor;
        if (mode == "always" || mode == "all")
            return ScriptUpdateMode::Always;
        if (!mode.empty() && mode != "play")
            PE_WARN("[Lua] script.on_update: unknown mode '%s', defaulting to play", mode.c_str());
        return ScriptUpdateMode::Play;
    }

    static const char *ScriptUpdateModeName(ScriptUpdateMode mode)
    {
        switch (mode)
        {
        case ScriptUpdateMode::Play:
            return "play";
        case ScriptUpdateMode::Editor:
            return "editor";
        case ScriptUpdateMode::Always:
            return "always";
        default:
            return "play";
        }
    }

    static sol::object MakeMeshBindingObject(sol::state_view lua, Scene &scene, NodeId *node)
    {
        int meshRef = scene.GetMeshRef(node);
        if (meshRef < 0)
            return sol::make_object(lua, sol::nil);

        const auto &meshes = scene.GetMeshes();
        if (meshRef >= static_cast<int>(meshes.size()))
            return sol::make_object(lua, sol::nil);

        const auto &mesh = meshes[meshRef];
        sol::table table = lua.create_table();
        table["index"] = meshRef;
        table["vertex_count"] = mesh.vertexCount;
        table["index_count"] = mesh.indexCount;
        table["bounding_box"] = lua.create_table_with(
            "min", mesh.boundingBox.min,
            "max", mesh.boundingBox.max,
            "center", mesh.boundingBox.GetCenter(),
            "size", mesh.boundingBox.GetSize());
        return sol::make_object(lua, table);
    }

    Image *LuaImage::Get()
    {
        return ptr;
    }

    template <typename... Args>
    static sol::protected_function_result CallProtected(const sol::function &fn, Args &&...args)
    {
        sol::protected_function protectedFn(fn);
        return protectedFn(std::forward<Args>(args)...);
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

        // hooks {} keyword - lets scripts register lifecycle hooks from local functions
        m_lua.set_function("hooks", [](sol::table t, sol::this_environment te)
                           {
            sol::environment env = te;
            // Use raw Lua C API to avoid sol2 string interning issues
            lua_State *L = env.lua_state();
            env.push(L);
            lua_pushliteral(L, "__hooks");
            t.push(L);
            lua_rawset(L, -3);
            lua_pop(L, 1); });

        // exposed {} keyword - declares variables to show in the editor's Properties panel.
        // Returns the same table so scripts can hold a live reference:
        //   local props = exposed { speed = 5.0, enabled = true }
        //   -- props.speed always reflects the current editor value
        m_lua.set_function("exposed", [](sol::table t, sol::this_environment te) -> sol::table
                           {
            sol::environment env = te;
            // Use raw Lua C API to set "__exposed" with a properly interned string key.
            // sol2's raw_set creates non-interned string keys in Lua 5.4, causing
            // lua_rawget with lua_pushliteral to fail on the same key.
            lua_State *L = env.lua_state();
            env.push(L);
            lua_pushliteral(L, "__exposed");
            t.push(L);
            lua_rawset(L, -3);
            lua_pop(L, 1);
            return t; });

        sol::table script = m_lua.create_named_table("script");
        script.set_function("on_update",
                            [this](const std::string &id, sol::function fn, sol::optional<std::string> mode)
                            { RegisterUpdateCallback(id, std::move(fn), mode.value_or("play")); });
        script.set_function("remove_update", [this](const std::string &id)
                            { UnregisterUpdateCallback(id); });
        script.set_function("clear_updates", [this]()
                            { m_registeredUpdates.clear(); });

        // Execute all registered binding functions
        for (auto &fn : GetBindings())
            fn(m_lua);

        LoadScripts();

        m_initialized = true;
    }

    // Helper: fetch a named key from a Lua table using raw C API to avoid sol2 string interning issues
    static sol::object RawGetLiteral(lua_State *L, sol::table &tbl, const char *key)
    {
        tbl.push(L);
        lua_pushstring(L, key);
        lua_rawget(L, -2);
        sol::object result = sol::stack::pop<sol::object>(L);
        lua_pop(L, 1); // pop table
        return result;
    }

    void ScriptSystem::CollectHooks(ScriptEntry &entry)
    {
        // If the script used hooks{}, read from that table; otherwise fall back to env
        lua_State *L = m_lua.lua_state();
        sol::object hooksObj = RawGetLiteral(L, entry.env, "__hooks");
        bool hasHooksTable = hooksObj.is<sol::table>();
        sol::table hooksTable = hasHooksTable ? hooksObj.as<sol::table>() : sol::table{};

        auto get = [&](const char *name) -> sol::function
        {
            sol::object obj = hasHooksTable ? hooksTable[name] : entry.env[name];
            return obj.is<sol::function>() ? obj.as<sol::function>() : sol::function{};
        };

        entry.initFn = get("init");
        entry.updateFn = get("update");
        entry.updateEditModeFn = get("update_editor");
        entry.destroyFn = get("destroy");
    }

    void ScriptSystem::CollectExposedVars(ScriptEntry &entry)
    {
        entry.exposedVars.clear();

        sol::object exposedObj = RawGetLiteral(m_lua.lua_state(), entry.env, "__exposed");
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

    void ScriptSystem::CollectHooks(NodeScriptInstance &inst)
    {
        lua_State *L = m_lua.lua_state();
        sol::object hooksObj = RawGetLiteral(L, inst.env, "__hooks");
        bool hasHooksTable = hooksObj.is<sol::table>();
        sol::table hooksTable = hasHooksTable ? hooksObj.as<sol::table>() : sol::table{};

        auto get = [&](const char *name) -> sol::function
        {
            sol::object obj = hasHooksTable ? hooksTable[name] : inst.env[name];
            return obj.is<sol::function>() ? obj.as<sol::function>() : sol::function{};
        };

        inst.initFn = get("init");
        inst.updateFn = get("update");
        inst.updateEditModeFn = get("update_editor");
        inst.destroyFn = get("destroy");
    }

    void ScriptSystem::CollectExposedVars(NodeScriptInstance &inst)
    {
        inst.exposedVars.clear();

        // Release old registry reference
        if (inst.exposedRef != LUA_NOREF)
        {
            luaL_unref(m_lua.lua_state(), LUA_REGISTRYINDEX, inst.exposedRef);
            inst.exposedRef = LUA_NOREF;
        }

        // Retrieve __exposed table via raw Lua C API to avoid sol2 string issues
        lua_State *L = m_lua.lua_state();
        inst.env.push(L);
        lua_pushliteral(L, "__exposed");
        lua_rawget(L, -2);
        if (!lua_istable(L, -1))
        {
            // Fallback: iterate env keys to handle sol2 string interning mismatches
            lua_pop(L, 1); // pop nil result
            bool found = false;
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                if (lua_type(L, -2) == LUA_TSTRING)
                {
                    const char *k = lua_tostring(L, -2);
                    if (k && strcmp(k, "__exposed") == 0)
                    {
                        found = true;
                        inst.exposedRef = luaL_ref(L, LUA_REGISTRYINDEX);
                        lua_pop(L, 1); // pop key
                        break;
                    }
                }
                lua_pop(L, 1);
            }
            if (!found)
            {
                lua_pop(L, 1); // pop env
                return;
            }
            lua_pop(L, 1); // pop env
        }
        else
        {
            // Store a registry reference to the exposed table
            inst.exposedRef = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pop(L, 1); // pop environment
        }

        // Now iterate the table for metadata
        lua_rawgeti(L, LUA_REGISTRYINDEX, inst.exposedRef);
        sol::table t(L, -1);
        lua_pop(L, 1);
        for (auto &[k, v] : t)
        {
            if (!k.is<std::string>())
                continue;

            ExposedVar var;
            var.name = k.as<std::string>();

            if (v.is<bool>())
                var.type = ExposedVar::Type::Bool;
            else if (v.is<double>())
                var.type = ExposedVar::Type::Number;
            else if (v.is<std::string>())
                var.type = ExposedVar::Type::String;
            else
                continue;

            inst.exposedVars.push_back(std::move(var));
        }
    }

    NodeScriptInstance ScriptSystem::CreateNodeInstance(NodeId *node, const std::string &path)
    {
        Scene *scene = GetActiveScene();
        if (!scene)
            return {};

        NodeScriptInstance inst;
        inst.handle = scene->MakeHandle(node);
        inst.sourcePath = NormalizeSlashes(path);
        // Node-script paths are stored project-root-relative (e.g. "Assets/Scripts/x.lua"). Resolve
        // them against the asset root, not the process CWD: NormalizePath()/weakly_canonical resolve
        // relative paths against the CWD, which is the bin dir on desktop (so it happens to work) but
        // "/" on a packaged player (Android), where the raw relative path fails to open.
        std::filesystem::path scriptPath(path);
        if (scriptPath.is_relative())
            scriptPath = std::filesystem::path(Path::Root) / scriptPath;
        inst.path = NormalizePath(scriptPath.string());

        // Fresh environment inheriting globals (bindings, pe_log, etc.)
        inst.env = sol::environment(m_lua, sol::create, m_lua.globals());

        // Inject node-local component references before the script runs so top-level
        // code can use them immediately without touching shared global state.
        RefreshNodeInstanceBindings(inst);

        // Execute the script file in this instance's private environment
        auto result = m_lua.safe_script_file(inst.path, inst.env, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            Log::Error(PeFormat("[Lua] node script error in '%s': %s", inst.path.c_str(), err.what()));
            inst.lastError = err.what();
            return inst; // no hooks/vars to collect from a failed script
        }
        else
        {
            inst.lastError.clear();
        }

        CollectHooks(inst);
        CollectExposedVars(inst);
        return inst;
    }

    void ScriptSystem::RefreshNodeInstanceBindings(NodeScriptInstance &inst)
    {
        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        if (!inst.handle.IsValid(*scene))
        {
            if (inst.bindingsValid)
            {
                inst.env["self"] = sol::lua_nil;
                inst.env["transform"] = sol::lua_nil;
                inst.env["mesh"] = sol::lua_nil;
                inst.env["camera"] = sol::lua_nil;
                inst.bindingsValid = false;
                inst.bindingComponentFlags = 0;
                StoreMeshBindingPayload(*scene, inst, -1);
                inst.bindingCamera = nullptr;
            }
            return;
        }

        NodeId *node = inst.handle.nodeId;
        const uint32_t flags = scene->GetComponentFlags(node);
        const int meshRef = scene->GetMeshRef(node);

        Camera *camera = nullptr;
        if (flags & Component_Camera)
            camera = scene->GetCameraForNode(node);

        if (!inst.bindingsValid)
        {
            inst.env["self"] = inst.handle;
            inst.env["transform"] = inst.handle;
        }

        const uint32_t bindingRelevantFlags = flags & (Component_Mesh | Component_Camera);
        const uint32_t previousRelevantFlags = inst.bindingComponentFlags & (Component_Mesh | Component_Camera);
        const bool meshChanged =
            bindingRelevantFlags != previousRelevantFlags || MeshBindingPayloadChanged(*scene, inst, meshRef);
        if (!inst.bindingsValid || meshChanged)
            inst.env["mesh"] = MakeMeshBindingObject(m_lua, *scene, node);

        if (!inst.bindingsValid || bindingRelevantFlags != previousRelevantFlags || camera != inst.bindingCamera)
            inst.env["camera"] = camera;

        inst.bindingComponentFlags = flags;
        StoreMeshBindingPayload(*scene, inst, meshRef);
        inst.bindingCamera = camera;
        inst.bindingsValid = true;
    }

    void ScriptSystem::InitializeNodeInstance(NodeScriptInstance &inst)
    {
        if (inst.initCalled)
            return;

        inst.initCalled = true;
        if (!inst.initFn.valid())
            return;

        auto res = CallProtected(inst.initFn);
        if (!res.valid())
        {
            sol::error err = res;
            Log::Error(PeFormat("[Lua] init() error in node script '%s': %s", inst.path.c_str(), err.what()));
            inst.lastError = err.what();
        }
    }

    void ScriptSystem::ReconcileNodeInstances()
    {
        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        uint32_t nodeCount = scene->GetNodeCount();

        // Remove instances whose handle is stale (scene reloaded / node deleted / script changed)
        for (auto it = m_nodeInstances.begin(); it != m_nodeInstances.end();)
        {
            bool valid = false;
            if (it->handle.IsValid(*scene))
            {
                uint32_t flags = scene->GetComponentFlags(it->handle.nodeId);
                if (flags & Component_Script)
                {
                    const std::string &currentPath = scene->GetNodeScriptPath(it->handle.nodeId);
                    if (MatchesNodeScriptPath(currentPath, *it))
                        valid = true;
                }
            }

            if (!valid)
            {
                if (it->initCalled && it->destroyFn.valid())
                {
                    auto res = CallProtected(it->destroyFn);
                    if (!res.valid())
                    {
                        sol::error err = res;
                        Log::Error(PeFormat("[Lua] destroy() error in node script '%s': %s", it->path.c_str(), err.what()));
                        it->lastError = err.what();
                    }
                }
                PE_INFO("[ScriptSystem] Removing stale node instance '%s'", it->path.c_str());
                if (it->exposedRef != LUA_NOREF)
                    luaL_unref(m_lua.lua_state(), LUA_REGISTRYINDEX, it->exposedRef);
                it = m_nodeInstances.erase(it);
            }
            else
            {
                ++it;
            }
        }

        std::unordered_set<const NodeId *> existingNodeInstances;
        existingNodeInstances.reserve(m_nodeInstances.size());
        for (const auto &inst : m_nodeInstances)
            existingNodeInstances.insert(inst.handle.nodeId);

        // Create instances for nodes that have a script but no instance yet
        for (uint32_t i = 0; i < nodeCount; i++)
        {
            NodeId *node = scene->GetNodeId(i);
            uint32_t flags = scene->GetComponentFlags(node);
            if (!(flags & Component_Script))
                continue;

            const std::string &scriptPath = scene->GetNodeScriptPath(node);
            if (scriptPath.empty())
                continue;

            if (existingNodeInstances.find(node) != existingNodeInstances.end())
                continue;

            m_nodeInstances.push_back(CreateNodeInstance(node, scriptPath));
            existingNodeInstances.insert(node);
        }
    }

    NodeScriptInstance *ScriptSystem::FindNodeInstance(const NodeId *node)
    {
        for (auto &inst : m_nodeInstances)
        {
            if (inst.handle.nodeId == node && node && inst.handle.nodeRevision == node->revision)
                return &inst;
        }
        return nullptr;
    }

    bool ScriptSystem::InvokeNodeRuntimeUiAction(NodeId *node,
                                                 const std::string &functionName,
                                                 const std::string &actionName,
                                                 const RuntimeUiWidgetState &state,
                                                 const std::string &screenId,
                                                 const std::string &widgetId)
    {
        if (!m_initialized || !node || functionName.empty())
            return false;

        Scene *scene = GetActiveScene();
        if (!scene || !scene->IsNodeAlive(node))
            return false;

        ReconcileNodeInstances();
        NodeScriptInstance *inst = FindNodeInstance(node);
        if (!inst)
            return false;

        RefreshNodeInstanceBindings(*inst);
        InitializeNodeInstance(*inst);
        if (!inst->lastError.empty())
            return false;

        sol::function callback;
        sol::object hooksObj = RawGetLiteral(m_lua.lua_state(), inst->env, "__hooks");
        if (hooksObj.is<sol::table>())
        {
            sol::object hook = hooksObj.as<sol::table>()[functionName];
            if (hook.is<sol::function>())
                callback = hook.as<sol::function>();
        }
        if (!callback.valid())
        {
            sol::object fn = inst->env[functionName];
            if (fn.is<sol::function>())
                callback = fn.as<sol::function>();
        }

        if (!callback.valid())
        {
            PE_WARN("[Lua] Runtime UI action function '%s' not found in node script '%s'",
                    functionName.c_str(),
                    inst->sourcePath.c_str());
            return false;
        }

        sol::table event = m_lua.create_table();
        event["action"] = actionName.empty() ? "click" : actionName;
        event["screen"] = screenId;
        event["widget"] = widgetId;
        event["node"] = inst->handle;
        event["self"] = inst->handle;
        event["hovered"] = state.hovered;
        event["active"] = state.active;
        event["clicked"] = state.clicked;
        event["down"] = state.down;
        event["dragging"] = state.dragging;
        event["drag_started"] = state.dragStarted;
        event["drag_released"] = state.dragReleased;
        event["mouse_x"] = state.mouseX;
        event["mouse_y"] = state.mouseY;
        event["drag_delta_x"] = state.dragDeltaX;
        event["drag_delta_y"] = state.dragDeltaY;

        sol::table mouse = m_lua.create_table();
        mouse["x"] = state.mouseX;
        mouse["y"] = state.mouseY;
        event["mouse"] = mouse;

        sol::table dragDelta = m_lua.create_table();
        dragDelta["x"] = state.dragDeltaX;
        dragDelta["y"] = state.dragDeltaY;
        event["drag_delta"] = dragDelta;

        auto result = CallProtected(callback, event);
        if (!result.valid())
        {
            sol::error err = result;
            Log::Error(PeFormat("[Lua] Runtime UI action '%s' error in node script '%s': %s",
                                functionName.c_str(),
                                inst->path.c_str(),
                                err.what()));
            inst->lastError = err.what();
            return false;
        }
        return true;
    }

    void ScriptSystem::CallInit(InitScope scope)
    {
        ReconcileNodeInstances();

        if (scope == InitScope::AllScripts)
        {
            const bool inEditor = IsEditorHost();
            const bool playMode = IsScriptPlayMode();
            for (auto &script : m_scripts)
            {
                if (script.lifecycle == ScriptLifecycle::PlayerOnly && inEditor && !playMode)
                    continue;
                if (script.lifecycle == ScriptLifecycle::EditorOnly && !inEditor)
                    continue;
                if (script.initFn.valid() && !HasNodeInstanceForPath(m_nodeInstances, script.path))
                {
                    auto result = CallProtected(script.initFn);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        Log::Error(PeFormat("[Lua] init() error in '%s': %s", script.path.c_str(), err.what()));
                    }
                    else
                    {
                        script.initialized = true;
                    }
                }
            }
        }

        for (auto &inst : m_nodeInstances)
            InitializeNodeInstance(inst);
    }

    void ScriptSystem::OnPlayModeChanged(bool nowPlay)
    {
        if (!IsEditorHost())
            return;

        for (auto &script : m_scripts)
        {
            if (script.lifecycle != ScriptLifecycle::PlayerOnly)
                continue;

            if (nowPlay && !script.initialized && script.initFn.valid() &&
                !HasNodeInstanceForPath(m_nodeInstances, script.path))
            {
                auto result = CallProtected(script.initFn);
                if (!result.valid())
                {
                    sol::error err = result;
                    Log::Error(PeFormat("[Lua] init() error in '%s': %s", script.path.c_str(), err.what()));
                }
                else
                {
                    script.initialized = true;
                }
            }
            else if (!nowPlay && script.initialized)
            {
                if (script.destroyFn.valid())
                {
                    auto result = CallProtected(script.destroyFn);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        Log::Error(PeFormat("[Lua] destroy() error in '%s': %s", script.path.c_str(), err.what()));
                    }
                }
                script.initialized = false;
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

    void ScriptSystem::RegisterUpdateCallback(const std::string &id, sol::function fn, const std::string &mode)
    {
        if (id.empty())
        {
            PE_WARN("[Lua] script.on_update requires a non-empty id");
            return;
        }
        if (!fn.valid())
        {
            PE_WARN("[Lua] script.on_update('%s') requires a valid function", id.c_str());
            return;
        }

        const ScriptUpdateMode parsedMode = ParseScriptUpdateMode(mode);
        for (RegisteredScriptUpdate &registered : m_registeredUpdates)
        {
            if (registered.id == id)
            {
                registered.fn = std::move(fn);
                registered.mode = parsedMode;
                registered.disabled = false;
                return;
            }
        }

        RegisteredScriptUpdate registered{};
        registered.id = id;
        registered.fn = std::move(fn);
        registered.mode = parsedMode;
        m_registeredUpdates.push_back(std::move(registered));
    }

    void ScriptSystem::UnregisterUpdateCallback(const std::string &id)
    {
        m_registeredUpdates.erase(
            std::remove_if(m_registeredUpdates.begin(), m_registeredUpdates.end(),
                           [&id](const RegisteredScriptUpdate &registered)
                           { return registered.id == id; }),
            m_registeredUpdates.end());
    }

    void ScriptSystem::RunRegisteredUpdateCallbacks()
    {
        const bool playMode = IsScriptPlayMode();
        const bool paused = IsScriptPaused();

        std::vector<std::string> callbackIds;
        callbackIds.reserve(m_registeredUpdates.size());
        for (const RegisteredScriptUpdate &registered : m_registeredUpdates)
        {
            if (registered.disabled || !registered.fn.valid())
                continue;

            callbackIds.push_back(registered.id);
        }

        for (const std::string &id : callbackIds)
        {
            auto it = std::find_if(m_registeredUpdates.begin(), m_registeredUpdates.end(),
                                   [&id](const RegisteredScriptUpdate &registered)
                                   { return registered.id == id; });
            if (it == m_registeredUpdates.end() || it->disabled || !it->fn.valid())
                continue;

            const ScriptUpdateMode mode = it->mode;
            sol::function fn = it->fn;

            bool shouldRun = false;
            switch (mode)
            {
            case ScriptUpdateMode::Always:
                shouldRun = !playMode || !paused;
                break;
            case ScriptUpdateMode::Editor:
                shouldRun = !playMode;
                break;
            case ScriptUpdateMode::Play:
                shouldRun = playMode && !paused;
                break;
            }
            if (!shouldRun)
                continue;

            auto result = CallProtected(fn);
            if (!result.valid())
            {
                sol::error err = result;
                Log::Error(PeFormat("[Lua] script.on_update('%s', mode='%s') error: %s",
                                    id.c_str(), ScriptUpdateModeName(mode), err.what()));
                auto disableIt = std::find_if(m_registeredUpdates.begin(), m_registeredUpdates.end(),
                                              [&id](const RegisteredScriptUpdate &registered)
                                              { return registered.id == id; });
                if (disableIt != m_registeredUpdates.end())
                    disableIt->disabled = true;
            }
        }
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

        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        for (auto &c : completed)
        {
            if (!c.model)
                continue;

            // Discard if scene changed since request
            if (c.sceneGeneration != scene->GetGeneration())
            {
                delete c.model;
                c.model = nullptr;
                continue;
            }

            // GPU upload on main thread (textures were only decoded on the background thread)
#if defined(PE_ENABLE_ASSIMP)
            if (auto *assimp = dynamic_cast<ModelAssetAssimp *>(c.model))
                assimp->UploadGpu();
#endif

            c.handle = scene->AddModelDeferred(c.model);
        }

        // Callbacks
        for (auto &c : completed)
        {
            if (c.callback.valid())
            {
                sol::protected_function_result res;
                if (c.model)
                    res = CallProtected(c.callback, c.handle, sol::nil);
                else
                    res = CallProtected(c.callback, sol::nil, std::string("load failed or scene changed"));

                if (!res.valid())
                {
                    sol::error err = res;
                    Log::Error(PeFormat("[Lua] async callback error: %s", err.what()));
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
                    auto res = CallProtected(c.batchCallback, sol::as_table(c.batchState->models));
                    if (!res.valid())
                    {
                        sol::error err = res;
                        Log::Error(PeFormat("[Lua] batch load callback error: %s", err.what()));
                    }
                }
            }
        }

        if (m_pendingAsyncLoads.empty())
            SetScriptModelLoading(false);
    }

    void ScriptSystem::ProcessSceneLoads()
    {
        if (m_pendingSceneLoads.empty())
            return;

        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        for (auto it = m_pendingSceneLoads.begin(); it != m_pendingSceneLoads.end();)
        {
            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            std::unique_ptr<ScenePreloadHandle> preload(it->future.get());

            if (it->sceneGeneration != scene->GetGeneration())
            {
                // Scene changed since request — discard
                if (it->callback.valid())
                {
                    auto res = CallProtected(it->callback, std::string("scene changed during load"));
                    if (!res.valid())
                    {
                        sol::error err = res;
                        Log::Error(PeFormat("[Lua] scene load callback error: %s", err.what()));
                    }
                }
                it = m_pendingSceneLoads.erase(it);
                continue;
            }

            LoadSceneApply(std::move(*preload));

            SetScriptModelLoading(false);

            if (it->callback.valid())
            {
                auto res = CallProtected(it->callback, sol::nil); // nil = no error
                if (!res.valid())
                {
                    sol::error err = res;
                    Log::Error(PeFormat("[Lua] scene load callback error: %s", err.what()));
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

        // Reconcile per-node script instances with the scene
        ReconcileNodeInstances();

        for (auto &inst : m_nodeInstances)
        {
            RefreshNodeInstanceBindings(inst);
            InitializeNodeInstance(inst);
        }

        // Skip file-level hooks for scripts that have per-node instances
        // (those hooks run per-node instead)
        auto isNodeScript = [this](const ScriptEntry &s) -> bool
        {
            return HasNodeInstanceForPath(m_nodeInstances, s.path);
        };

        RunRegisteredUpdateCallbacks();

        // update_editor() runs every frame when not in play mode,
        // so scripts can provide edit-mode behavior without entering play mode.
        if (!IsScriptPlayMode())
        {
            for (auto &script : m_scripts)
            {
                if (script.updateEditModeFn.valid() && !isNodeScript(script))
                {
                    auto result = CallProtected(script.updateEditModeFn);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        Log::Error(PeFormat("[Lua] update_editor() error in '%s': %s", script.path.c_str(), err.what()));
                    }
                }
            }
            for (auto &inst : m_nodeInstances)
            {
                if (!inst.lastError.empty())
                    continue;
                if (inst.updateEditModeFn.valid())
                {
                    auto result = CallProtected(inst.updateEditModeFn);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        Log::Error(PeFormat("[Lua] update_editor() error in node script '%s': %s", inst.path.c_str(), err.what()));
                        inst.lastError = err.what();
                    }
                }
            }
        }

        if (IsScriptPlayMode() && !IsScriptPaused())
        {
            // update() runs only in play mode
            for (auto &script : m_scripts)
            {
                if (script.updateFn.valid() && !isNodeScript(script))
                {
                    auto result = CallProtected(script.updateFn);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        Log::Error(PeFormat("[Lua] update() error in '%s': %s", script.path.c_str(), err.what()));
                    }
                }
            }
            for (auto &inst : m_nodeInstances)
            {
                if (!inst.lastError.empty())
                    continue;
                if (inst.updateFn.valid())
                {
                    auto result = CallProtected(inst.updateFn);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        Log::Error(PeFormat("[Lua] update() error in node script '%s': %s", inst.path.c_str(), err.what()));
                        inst.lastError = err.what();
                    }
                }
            }
        }
    }

    void ScriptSystem::Destroy()
    {
        if (!m_initialized)
            return;

        for (auto &script : m_scripts)
        {
            if (script.destroyFn.valid() && !HasNodeInstanceForPath(m_nodeInstances, script.path))
            {
                auto result = CallProtected(script.destroyFn);
                if (!result.valid())
                {
                    sol::error err = result;
                    Log::Error(PeFormat("[Lua] destroy() error in '%s': %s", script.path.c_str(), err.what()));
                }
            }
        }

        for (auto &inst : m_nodeInstances)
        {
            if (inst.initCalled && inst.destroyFn.valid())
            {
                auto result = CallProtected(inst.destroyFn);
                if (!result.valid())
                {
                    sol::error err = result;
                    Log::Error(PeFormat("[Lua] destroy() error in node script '%s': %s", inst.path.c_str(), err.what()));
                    inst.lastError = err.what();
                }
            }
        }
        for (auto &inst : m_nodeInstances)
        {
            if (inst.exposedRef != LUA_NOREF)
                luaL_unref(m_lua.lua_state(), LUA_REGISTRYINDEX, inst.exposedRef);
        }
        m_nodeInstances.clear();

        // Wait for any pending async loads before destroying Lua state
        for (auto &load : m_pendingAsyncLoads)
            load.future.wait();
        m_pendingAsyncLoads.clear();

        // Wait for any pending async scene loads
        for (auto &load : m_pendingSceneLoads)
        {
            if (load.future.valid())
            {
                ScenePreloadHandle *p = load.future.get();
                delete p;
            }
        }
        m_pendingSceneLoads.clear();

        m_registeredUpdates.clear();
        m_scripts.clear();
        // Script render passes capture sol functions; release them before the state.
        ClearScriptRenderPasses();
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

    std::vector<std::string> ScriptSystem::ListLuaFunctions()
    {
        std::vector<std::string> functions;
        if (!m_initialized)
            return functions;

        lua_State *L = m_lua.lua_state();
        lua_getglobal(L, "_G");

        std::unordered_set<const void *> visitedTables;
        CollectLuaFunctions(L, -1, "", 3, functions, visitedTables);

        lua_pop(L, 1);

        std::sort(functions.begin(), functions.end());
        functions.erase(std::unique(functions.begin(), functions.end()), functions.end());
        return functions;
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

    bool ScriptSystem::IsTestScriptPath(const std::string &path)
    {
        const std::string normalized = NormalizePath(path);
        return normalized.find("/tests/") != std::string::npos || normalized.find("\\tests\\") != std::string::npos;
    }

    std::vector<std::string> ScriptSystem::GetTestScriptPaths() const
    {
        std::vector<std::string> paths;
        const std::filesystem::path testsDir = std::filesystem::path(Path::Assets) / "Scripts" / "tests";
        if (!std::filesystem::exists(testsDir))
            return paths;

        for (const auto &file : std::filesystem::recursive_directory_iterator(testsDir))
        {
            if (file.path().extension() != ".lua")
                continue;

            const std::string filePath = NormalizePath(file.path().string());
            if (IsSupportTestScriptPath(filePath))
                continue;

            paths.push_back(filePath);
        }

        std::sort(paths.begin(), paths.end());
        return paths;
    }

    std::string ScriptSystem::RunScriptTests(const std::vector<std::string> &paths)
    {
        if (!m_initialized)
            return "error: ScriptSystem not initialized";
        if (paths.empty())
            return "No script tests found.";

        std::vector<std::string> normalizedPaths;
        normalizedPaths.reserve(paths.size());
        for (const auto &path : paths)
        {
            if (path.empty())
                continue;

            const std::string normalizedPath = NormalizePath(path);
            if (!std::filesystem::exists(normalizedPath))
                return "error: missing test script: " + normalizedPath;

            normalizedPaths.push_back(normalizedPath);
        }

        if (normalizedPaths.empty())
            return "No script tests found.";

        std::sort(normalizedPaths.begin(), normalizedPaths.end());
        normalizedPaths.erase(std::unique(normalizedPaths.begin(), normalizedPaths.end()), normalizedPaths.end());

        std::string captured;
        auto appendLine = [&captured](const std::string &line)
        {
            if (line.empty())
                return;
            if (!captured.empty())
                captured += "\n";
            captured += line;
        };

        sol::function originalLog = m_lua["pe_log"];
        sol::function originalWarn = m_lua["pe_warn"];
        sol::function originalError = m_lua["pe_error"];

        m_lua.set_function("pe_log", [&appendLine](const std::string &msg)
                           { appendLine(msg); });
        m_lua.set_function("pe_warn", [&appendLine](const std::string &msg)
                           { appendLine("[WARN] " + msg); });
        m_lua.set_function("pe_error", [&appendLine](const std::string &msg)
                           { appendLine("[ERROR] " + msg); });

        const auto restoreLogging = [&]()
        {
            m_lua["pe_log"] = originalLog;
            m_lua["pe_warn"] = originalWarn;
            m_lua["pe_error"] = originalError;
        };

        sol::environment env(m_lua, sol::create, m_lua.globals());

        const std::string supportPath = NormalizePath((std::filesystem::path(Path::Assets) / "Scripts" / "tests" / "test_utils.lua").string());
        if (std::filesystem::exists(supportPath))
        {
            auto supportResult = m_lua.safe_script_file(supportPath, env, sol::script_pass_on_error);
            if (!supportResult.valid())
            {
                sol::error err = supportResult;
                restoreLogging();
                return "error: test_utils.lua: " + std::string(err.what());
            }
        }

        for (const auto &filePath : normalizedPaths)
        {
            auto loadResult = m_lua.safe_script_file(filePath, env, sol::script_pass_on_error);
            if (!loadResult.valid())
            {
                sol::error err = loadResult;
                appendLine("error: " + filePath + ": " + std::string(err.what()));
                continue;
            }

            const std::string runnerName = MakeTestRunnerName(filePath);
            sol::object runnerObj = env[runnerName];
            if (!runnerObj.is<sol::function>())
                continue;

            auto runnerResult = CallProtected(runnerObj.as<sol::function>());
            if (!runnerResult.valid())
            {
                sol::error err = runnerResult;
                appendLine("error: " + runnerName + "(): " + std::string(err.what()));
            }
        }

        restoreLogging();

        if (captured.empty())
            captured = "ok";

        return captured;
    }

    void ScriptSystem::LoadScripts()
    {
        if (IsEditorHost())
        {
            const std::string &editorAssetsRoot = GetEditorScriptAssetsRoot();
            LoadScriptsFromDir(MakeScriptDir(editorAssetsRoot, "global"), ScriptLifecycle::EditorOnly);
            LoadScriptsFromDir(MakeScriptDir(editorAssetsRoot, "Editor"), ScriptLifecycle::EditorOnly);
        }

        LoadScriptsFromDir(MakeScriptDir(Path::Assets, "global"), ScriptLifecycle::Always);
        if (IsEditorHost())
            LoadScriptsFromDir(MakeScriptDir(Path::Assets, "Editor"), ScriptLifecycle::EditorOnly);
        // PlayerOnly scripts auto-load always (so their globals exist), but their init is
        // deferred to play-mode entry in the editor; in the player they fire as normal.
        LoadScriptsFromDir(MakeScriptDir(Path::Assets, "Player"), ScriptLifecycle::PlayerOnly);
    }

    void ScriptSystem::LoadScriptsFromDir(const std::string &dir, ScriptLifecycle lifecycle)
    {
        if (dir.empty() || !std::filesystem::exists(dir))
            return;

        for (auto &file : std::filesystem::recursive_directory_iterator(dir))
        {
            if (file.path().extension() != ".lua")
                continue;

            std::string filePath = NormalizePath(file.path().string());
            if (HasLoadedScriptPath(m_scripts, filePath))
                continue;

            if (lifecycle == ScriptLifecycle::Always &&
                !ShouldLoadGlobalScript(file.path(), filePath, true))
                continue;

            sol::environment env(m_lua, sol::create, m_lua.globals());

            auto result = m_lua.safe_script_file(filePath, env, sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error err = result;
                Log::Error(PeFormat("[Lua] script error in '%s': %s", filePath.c_str(), err.what()));
                continue;
            }

            static const std::set<std::string> hookNames = {"init", "update", "update_editor", "destroy"};
            for (auto &[key, val] : env)
            {
                if (key.is<std::string>())
                {
                    std::string name = key.as<std::string>();
                    if (hookNames.find(name) != hookNames.end())
                        continue;
                    if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
                        continue;
                    sol::object existing = m_lua.globals().raw_get<sol::object>(name);
                    if (existing.valid() && existing.get_type() != sol::type::lua_nil)
                        PE_WARN("[Lua] global '%s' redefined by '%s'", name.c_str(), filePath.c_str());
                    m_lua.globals()[name] = val;
                }
            }

            ScriptEntry entry;
            entry.path = filePath;
            entry.env = std::move(env);
            entry.lifecycle = lifecycle;
            CollectHooks(entry);
            CollectExposedVars(entry);
            m_scripts.push_back(std::move(entry));

            PE_INFO("Loaded Lua script: %s", filePath.c_str());
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

        bool foundNew = false;
        std::vector<std::string> globalDirs;
        if (IsEditorHost())
            globalDirs.push_back(MakeScriptDir(GetEditorScriptAssetsRoot(), "global"));
        globalDirs.push_back(MakeScriptDir(Path::Assets, "global"));

        for (const std::string &globalDir : globalDirs)
        {
            if (globalDir.empty() || !std::filesystem::exists(globalDir))
                continue;

            for (auto &file : std::filesystem::recursive_directory_iterator(globalDir))
            {
                if (file.path().extension() != ".lua")
                    continue;

                std::string filePath = NormalizePath(file.path().string());
                if (!ShouldLoadGlobalScript(file.path(), filePath, false))
                    continue;

                if (!HasLoadedScriptPath(m_scripts, filePath))
                {
                    FileWatcher::Add(filePath, [](size_t fileEvent)
                                     {
                        EventSystem::PushEvent(fileEvent);
                        EventSystem::PushEvent(EventType::CompileScripts); });

                    foundNew = true;
                    PE_INFO("New Lua script detected: %s", filePath.c_str());
                }
            }
        }

        if (foundNew)
            Reload();
    }
} // namespace pe
