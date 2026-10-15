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
        // False while the window has no drawable area: minimized, or hidden by the OS (Win+D keeps SDL's minimized
        // flag clear, so the surface is asked too). Nothing may be rendered or its swapchain rebuilt then.
        bool IsRenderable() const;
        void GetDrawableSize(int &width, int &height);
        void Show();
        void Hide();
        void Minimize();
        void Maximize();

    private:
        EventSystem::CallbackToken m_setTitleToken{0};
        bool m_owned = true;
        bool m_resizeDeferred = false;
    };
} // namespace pe
