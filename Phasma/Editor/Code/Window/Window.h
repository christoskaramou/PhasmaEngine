#pragma once

namespace pe
{
    class Context;
    class Camera;
    class Window : public PeHandle<Window, SDL_Window *>
    {
    public:
        Window(int x, int y, int w, int h, uint32_t flags);
        Window(SDL_Window *existing); // non-owning adopt
        ~Window();

        static Window *Adopt(SDL_Window *existing) { return new Window(existing); }

        void SmoothMouseRotation(Camera *camera, uint32_t triggerButton);
        bool ProcessEvents();
        void WrapMouse(int &x, int &y);
        bool IsInsideRenderWindow(int x, int y);
        bool isMinimized();
        void GetDrawableSize(int &width, int &height);
        void Show();
        void Hide();
        void Minimize();
        void Maximize();

    private:
        EventSystem::CallbackToken m_setTitleToken{0};
        bool m_owned = true;
    };
} // namespace pe
