#pragma once

namespace pe
{
    class GUI;
    class CommandBuffer;

    class Widget
    {
    public:
        Widget(const std::string &name) : m_name(name) {}
        virtual ~Widget() = default;

        virtual void Init(GUI *gui) { m_gui = gui; }
        virtual void Update() {}
        virtual void Draw(CommandBuffer *cmd) {}

        bool IsOpen() { return m_open; }
        bool *GetOpen() { return &m_open; }

        void SetName(const std::string &name) { m_name = name; }
        const std::string &GetName() { return m_name; }

    protected:
        bool m_open = true;
        std::string m_name;
        GUI *m_gui = nullptr;
    };
} // namespace pe
