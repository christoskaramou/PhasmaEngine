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

        bool Frame();

        void Run();

    private:
        Context *m_context = nullptr;
        Window *m_window = nullptr;
        FrameTimer &m_frameTimer;
        SplashScreen *m_splashScreen = nullptr;
    };
}