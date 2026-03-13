#pragma once

#include <sol/sol.hpp>

namespace pe
{
    class ScriptManager
    {
    public:
        static void Init();
        static void Update();
        static void Draw();
        static void Destroy();
        static void Reload();

        inline static sol::state &GetLua() { return s_lua; }

    private:
        static void LoadScripts();
        static void RegisterBindings();

        inline static sol::state s_lua{};
        inline static std::vector<std::string> m_scriptPaths{};
        inline static bool m_initialized = false;
    };
} // namespace pe
