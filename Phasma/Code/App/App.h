#pragma once

namespace pe
{
    class Context;
    class Window;
    class FrameTimer;

    class App
    {
    public:
        App();

        ~App();

        void Run();

    private:
        Context *context = nullptr;
        Window *window = nullptr;
        FrameTimer *frameTimer = nullptr;
    };
}