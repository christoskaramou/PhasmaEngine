#pragma once

namespace pe
{
    class GUI;
    class CommandBuffer;

    class Widget
    {
    public:
        Widget() = default;
        virtual ~Widget() = default;

        virtual void Init(GUI *gui) { m_gui = gui; }
        virtual void Update() {}
        virtual void Draw(CommandBuffer *cmd) {}

        bool IsOpen() { return m_open; }

    protected:
        bool m_open = true;
        GUI *m_gui = nullptr;
    };
} // namespace pe
