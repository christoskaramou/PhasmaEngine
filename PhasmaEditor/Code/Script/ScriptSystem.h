#pragma once

#if defined(PE_SCRIPTS)
#include <sol/sol.hpp>

namespace pe
{
    using LuaBindingFunc = std::function<void(sol::state &)>;

    class CommandBuffer;
    class Image;

    struct LuaImage
    {
        Image *ptr = nullptr;
        bool owned = true;
        bool useRenderScale = true;
        std::string rtName;

        Image *Get();
    };

    class ScriptSystem : public ISystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Reload();

        sol::state &GetLua() { return m_lua; }
        bool IsInitialized() const { return m_initialized; }

        // Execute Lua code and return captured output + return value
        std::string ExecuteLua(const std::string &code);

        static void AddBindings(LuaBindingFunc func);

    private:
        void LoadScripts();
        static std::vector<LuaBindingFunc> &GetBindings();

        sol::state m_lua{};
        std::vector<std::string> m_scriptPaths{};
        bool m_initialized = false;
    };
} // namespace pe
#endif
