#pragma once

#if defined(PE_SCRIPTS)
#include <sol/sol.hpp>
#include <future>

namespace pe
{
    using LuaBindingFunc = std::function<void(sol::state &)>;

    class CommandBuffer;
    class Image;
    class Model;

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
        std::vector<Model *> models;
    };

    struct PendingAsyncLoad
    {
        std::shared_future<Model *> future;
        sol::function callback;
        // For batch loading (load_models)
        std::shared_ptr<BatchLoadState> batchState;
        sol::function batchCallback;
    };

    struct ScriptEntry
    {
        std::string path;
        sol::environment env;
        sol::function initFn;
        sol::function updateFn;
        sol::function updateEditorFn;
        sol::function destroyFn;
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

        static void AddBindings(LuaBindingFunc func);

    private:
        void LoadScripts();
        void ScanForNewScripts();
        void CollectHooks(ScriptEntry &entry);
        static std::vector<LuaBindingFunc> &GetBindings();

        sol::state m_lua{};
        std::vector<ScriptEntry> m_scripts{};
        std::vector<PendingAsyncLoad> m_pendingAsyncLoads;
        bool m_initialized = false;
        double m_scanTimer = 0.0;
    };
} // namespace pe
#endif
