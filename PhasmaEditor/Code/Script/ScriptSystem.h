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

    class ScriptSystem : public ISystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void InitRestricted(CommandBuffer *cmd);
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

        // Returns the ScriptEntry whose path matches, or nullptr if not found
        ScriptEntry *FindScript(const std::string &path);

        static void AddBindings(LuaBindingFunc func);

    private:
        void LoadScripts();
        void ScanForNewScripts();
        void CollectHooks(ScriptEntry &entry);
        void CollectExposedVars(ScriptEntry &entry);
        static std::vector<LuaBindingFunc> &GetBindings();
        void InitInternal(CommandBuffer *cmd, bool restricted);

        void ProcessSceneLoads();

        sol::state m_lua{};
        std::vector<ScriptEntry> m_scripts{};
        std::vector<PendingAsyncLoad> m_pendingAsyncLoads;
        std::vector<PendingSceneLoad> m_pendingSceneLoads;
        bool m_initialized = false;
        double m_scanTimer = 0.0;
    };
} // namespace pe
