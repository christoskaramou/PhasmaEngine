#pragma once

#include <sol/sol.hpp>
#include "Scene/SceneNodeHandle.h"
#include "Scene/Scene.h"
#include "Scene/SceneHost.h"

namespace pe
{
    using LuaBindingFunc = std::function<void(sol::state &)>;

    class CommandBuffer;
    class Camera;
    class Image;
    class ModelAsset;
    struct RuntimeUiWidgetState;

    struct LuaImage
    {
        Image *ptr = nullptr;
        bool owned = true;
        bool useRenderScale = true;
        std::string rtName;

        Image *Get();
    };

    struct BatchLoadState
    {
        int total = 0;
        int completed = 0;
        std::vector<SceneNodeHandle> models;
    };

    struct PendingAsyncLoad
    {
        std::shared_future<ModelAsset *> future;
        sol::function callback;
        // For batch loading (load_models)
        std::shared_ptr<BatchLoadState> batchState;
        sol::function batchCallback;
        uint32_t sceneGeneration = 0;
    };

    struct PendingSceneLoad
    {
        std::shared_future<ScenePreloadHandle *> future;
        sol::function callback;
        uint32_t sceneGeneration = 0;
    };

    struct ExposedVar
    {
        enum class Type
        {
            Number,
            Bool,
            String
        };
        std::string name;
        Type type;
    };

    enum class ScriptLifecycle
    {
        Always,
        PlayerOnly,
        EditorOnly,
    };

    struct ScriptEntry
    {
        std::string path;
        sol::environment env;
        sol::function initFn;
        sol::function updateFn;
        sol::function updateEditModeFn;
        sol::function destroyFn;
        std::vector<ExposedVar> exposedVars;
        ScriptLifecycle lifecycle = ScriptLifecycle::Always;
        bool initialized = false;
    };

    enum class ScriptUpdateMode
    {
        Play,
        Editor,
        Always
    };

    struct RegisteredScriptUpdate
    {
        std::string id;
        sol::function fn;
        ScriptUpdateMode mode = ScriptUpdateMode::Play;
        bool disabled = false;
    };

    // Per-node script instance — each node with Component_Script gets its own
    // isolated environment even if two nodes reference the same .lua file.
    struct NodeScriptInstance
    {
        SceneNodeHandle handle;
        std::string path;
        std::string sourcePath;
        sol::environment env;
        sol::function initFn;
        sol::function updateFn;
        sol::function updateEditModeFn;
        sol::function destroyFn;
        std::vector<ExposedVar> exposedVars;
        int exposedRef = LUA_NOREF; // Lua registry ref to __exposed table
        bool initCalled = false;
        bool bindingsValid = false;
        uint32_t bindingComponentFlags = 0;
        int bindingMeshRef = -1;
        uint32_t bindingMeshVertexCount = 0;
        uint32_t bindingMeshIndexCount = 0;
        AABB bindingMeshBounds{};
        Camera *bindingCamera = nullptr;
        std::string lastError; // non-empty when the script has an active error
    };

    class ScriptSystem : public ISystem
    {
    public:
        enum class InitScope
        {
            AllScripts,
            ActiveNodeScriptsOnly
        };

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Reload();
        void CallInit(InitScope scope = InitScope::AllScripts);
        std::vector<std::string> GetTestScriptPaths() const;
        std::string RunScriptTests(const std::vector<std::string> &paths);

        sol::state &GetLua() { return m_lua; }
        bool IsInitialized() const { return m_initialized; }
        static bool IsTestScriptPath(const std::string &path);
        std::vector<std::string> ListLuaFunctions();

        // Execute Lua code and return captured output + return value
        std::string ExecuteLua(const std::string &code);

        // Async model loading coroutine support
        void AddPendingAsyncLoad(PendingAsyncLoad load);
        void ProcessAsyncLoads();

        // Async scene loading support
        void AddPendingSceneLoad(PendingSceneLoad load);

        // Per-node instance lookup — returns the instance for a specific node, or nullptr
        NodeScriptInstance *FindNodeInstance(const NodeId *node);
        bool InvokeNodeRuntimeUiAction(NodeId *node,
                                       const std::string &functionName,
                                       const std::string &actionName,
                                       const RuntimeUiWidgetState &state,
                                       const std::string &screenId,
                                       const std::string &widgetId);

        static void AddBindings(LuaBindingFunc func);

        // Fire pending init for PlayerOnly scripts on entry, destroy on exit.
        void OnPlayModeChanged(bool nowPlay);

    private:
        void LoadScripts();
        void LoadScriptsFromDir(const std::string &dir, ScriptLifecycle lifecycle);
        void ScanForNewScripts();
        void CollectHooks(ScriptEntry &entry);
        void CollectExposedVars(ScriptEntry &entry);
        void CollectHooks(NodeScriptInstance &inst);
        void CollectExposedVars(NodeScriptInstance &inst);
        static std::vector<LuaBindingFunc> &GetBindings();

        void ProcessSceneLoads();
        void RegisterUpdateCallback(const std::string &id, sol::function fn, const std::string &mode);
        void UnregisterUpdateCallback(const std::string &id);
        void RunRegisteredUpdateCallbacks();

        // Per-node script instance management
        void ReconcileNodeInstances();
        NodeScriptInstance CreateNodeInstance(NodeId *node, const std::string &path);
        void RefreshNodeInstanceBindings(NodeScriptInstance &inst);
        void InitializeNodeInstance(NodeScriptInstance &inst);
        void DestroyNodeInstance(NodeScriptInstance &inst);

        sol::state m_lua{};
        std::vector<ScriptEntry> m_scripts{};
        std::vector<NodeScriptInstance> m_nodeInstances{};
        std::vector<RegisteredScriptUpdate> m_registeredUpdates{};
        std::vector<PendingAsyncLoad> m_pendingAsyncLoads;
        std::vector<PendingSceneLoad> m_pendingSceneLoads;
        bool m_initialized = false;
        double m_scanTimer = 0.0;
    };
} // namespace pe
