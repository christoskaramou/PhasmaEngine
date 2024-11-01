#pragma once

namespace pe
{
    using ScriptFunc_Void = void (*)();

    class Script
    {
    public:
        Script(const std::string &path);

        ScriptFunc_Void Init = nullptr;
        ScriptFunc_Void Update = nullptr;
        ScriptFunc_Void Draw = nullptr;
        ScriptFunc_Void Destroy = nullptr;

    private:
        friend class ScriptManager;

        std::string m_path;
        size_t m_hash;
    };

    class ScriptManager
    {
    public:
        static void Init();
        static void CompileScript(const Script &script);
        static void LoadScript(const Script &script);
        static void UnloadScript(const Script &script);
        static void UnloadAllScripts();
        static void PollScripts();

    private:
        inline static std::map<size_t, Script> s_scripts{};
    };
}
