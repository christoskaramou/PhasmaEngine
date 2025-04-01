#pragma once

namespace pe
{
    class Context;
    class Camera;

    using WindowApiHandle = ApiHandle<ObjectType::Unknown, SDL_Window *, Placeholder<0> *>;
    class Window : public PeHandle<Window, WindowApiHandle>
    {
    public:
        Window(int x, int y, int w, int h, uint32_t flags);
        ~Window();

        void SmoothMouseRotation(Camera *camera, uint32_t triggerButton);
        bool ProcessEvents(double delta);
        void WrapMouse(int &x, int &y);
        bool IsInsideRenderWindow(int x, int y);
        bool isMinimized();
        void GetDrawableSize(int &width, int &height);
        void Show();
        void Hide();
        void Minimize();
        void Maximize();
    };
}
