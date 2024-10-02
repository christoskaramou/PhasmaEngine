#pragma once

namespace pe
{
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
        Window *m_window = nullptr;
        FrameTimer &m_frameTimer;
        SplashScreen *m_splashScreen = nullptr;
    };
}