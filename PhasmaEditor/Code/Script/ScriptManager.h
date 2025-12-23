#pragma once

namespace pe
{
    class Script;

    class ScriptManager
    {
    public:
        static void Init();
        static void Shutdown();

        inline static void *GetModule() { return s_module; }
        inline static const std::vector<Script *> &GetScripts() { return m_scripts; }

    private:
        static void LoadModule();
        static void UnloadModule();

        inline static void *s_module = nullptr;
        inline static std::vector<Script *> m_scripts{};
    };
} // namespace pe
