#pragma once

#include <sol/sol.hpp>
#include <future>
#include "Scene/SceneNodeHandle.h"
#include "Scene/Scene.h"

namespace pe
{
    using LuaBindingFunc = std::function<void(sol::state &)>;

    class CommandBuffer;
    class Image;
    class ModelAsset;

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
        std::shared_future<Scene::ScenePreload *> future;
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

    struct ScriptEntry
    {
        std::string path;
        sol::environment env;
        sol::function initFn;
        sol::function updateFn;
        sol::function updateEditorFn;
        sol::function destroyFn;
        std::vector<ExposedVar> exposedVars;
    };

    // Per-node script instance — each node with Component_Script gets its own
    // isolated environment even if two nodes reference the same .lua file.
    struct NodeScriptInstance
    {
        SceneNodeHandle handle;
        std::string path;
        sol::environment env;
        sol::function initFn;
        sol::function updateFn;
        sol::function updateEditorFn;
        sol::function destroyFn;
        std::vector<ExposedVar> exposedVars;
        int exposedRef = LUA_NOREF; // Lua registry ref to __exposed table
        bool initCalled = false;
    };

    class ScriptSystem : public ISystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Reload();
        void CallInit();

        sol::state &GetLua() { return m_lua; }
        bool IsInitialized() const { return m_initialized; }

        // Execute Lua code and return captured output + return value
        std::string ExecuteLua(const std::string &code);

        // Async model loading coroutine support
        void AddPendingAsyncLoad(PendingAsyncLoad load);
        void ProcessAsyncLoads();

        // Async scene loading support
        void AddPendingSceneLoad(PendingSceneLoad load);

        // Per-node instance lookup — returns the instance for a specific node, or nullptr
        NodeScriptInstance *FindNodeInstance(const NodeId *node);

        static void AddBindings(LuaBindingFunc func);

    private:
        void LoadScripts();
        void ScanForNewScripts();
        void CollectHooks(ScriptEntry &entry);
        void CollectExposedVars(ScriptEntry &entry);
        void CollectHooks(NodeScriptInstance &inst);
        void CollectExposedVars(NodeScriptInstance &inst);
        static std::vector<LuaBindingFunc> &GetBindings();

        void ProcessSceneLoads();

        // Per-node script instance management
        void ReconcileNodeInstances();
        NodeScriptInstance CreateNodeInstance(NodeId *node, const std::string &path);
        void RefreshNodeInstanceBindings(NodeScriptInstance &inst);
        void InitializeNodeInstance(NodeScriptInstance &inst);

        sol::state m_lua{};
        std::vector<ScriptEntry> m_scripts{};
        std::vector<NodeScriptInstance> m_nodeInstances{};
        std::vector<PendingAsyncLoad> m_pendingAsyncLoads;
        std::vector<PendingSceneLoad> m_pendingSceneLoads;
        bool m_initialized = false;
        double m_scanTimer = 0.0;
    };
} // namespace pe
