#pragma once


namespace pe
{
    class Window;
    class FrameTimer;
    class SplashScreen;
    class RuntimeUiSystem;

    class App
    {
    public:
        App();
        ~App();

        bool Frame();
        void Run();
        void RenderReloadFrame();
        void ReleaseImGuiContext();

    private:
        Window *m_window = nullptr;
        FrameTimer &m_frameTimer;
        SplashScreen *m_splashScreen = nullptr;
        std::unique_ptr<RuntimeUiSystem> m_runtimeUi;
        uint32_t m_startupPresentRefreshFrames = 0;
    };
} // namespace pe
