#pragma once

namespace pe
{
    class ScriptObject;

    class Script
    {
    public:
        Script(const std::string &path) : m_scriptObject{nullptr}, m_createObjectFunc{nullptr}, m_path{path} {}

        void Init();
        void Update();
        void Draw();
        void Destroy();

    private:
        friend class ScriptManager;

        using CreateObjectFunc = ScriptObject *(*)();

        void CreateObject();

        ScriptObject *m_scriptObject;
        CreateObjectFunc m_createObjectFunc;
        std::string m_path;
    };
} // namespace pe
