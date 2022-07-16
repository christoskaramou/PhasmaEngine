#pragma once

namespace pe
{
    class Context;

    class Window : public IHandle<Window, WindowHandle>
    {
    public:
        Window(int x, int y, int w, int h, uint32_t flags);

        ~Window();

        bool ProcessEvents(double delta);

        void WrapMouse(int &x, int &y);

        bool IsInsideRenderWindow(int x, int y);

        bool isMinimized();

        void SetTitle(const std::string &title);
    };
}