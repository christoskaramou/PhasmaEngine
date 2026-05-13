#pragma once

namespace pe::InputState
{
    struct MouseDelta
    {
        int x = 0;
        int y = 0;
    };

    void BeginFrame();
    void AddMouseMotion(int xrel, int yrel);
    MouseDelta ConsumeMouseDelta();
    void ResetMouseDelta();
} // namespace pe::InputState
