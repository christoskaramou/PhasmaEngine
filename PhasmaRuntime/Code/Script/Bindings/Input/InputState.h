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
    void AddMouseWheel(int x, int y);
    MouseDelta ConsumeMouseDelta();
    MouseDelta GetMouseWheel();
    void ResetMouseDelta();
    void SetMouseCapturedByUi(bool captured);
    void SetKeyboardCapturedByUi(bool captured);
    bool IsMouseCapturedByUi();
    bool IsKeyboardCapturedByUi();
} // namespace pe::InputState
