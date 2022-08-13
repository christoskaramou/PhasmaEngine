#pragma once

namespace pe
{
    class Context;
    class Window;
    class FrameTimer;
    class SplashScreen;

    class App
    {
    public:
        App();

        ~App();

        bool RenderFrame();

        void Run();

    private:
        Context *m_context = nullptr;
        Window *m_window = nullptr;
        FrameTimer *m_frameTimer = nullptr;
        SplashScreen *m_splashScreen = nullptr;
    };
}