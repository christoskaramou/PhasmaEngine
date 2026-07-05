#pragma once

namespace pe::InputState
{
    struct MouseDelta
    {
        int x = 0;
        int y = 0;
    };

    struct TouchState
    {
        float dx = 0.0f; // primary-finger normalized move this frame (fraction of surface)
        float dy = 0.0f;
        float pinch = 0.0f; // two-finger spread change this frame (fraction of surface diagonal)
        int fingers = 0;    // fingers currently down
    };

    void BeginFrame();
    void AddMouseMotion(int xrel, int yrel);
    void AddMouseWheel(int x, int y);
    MouseDelta ConsumeMouseDelta();
    MouseDelta PeekMouseDelta();
    MouseDelta GetMouseWheel();
    void ResetMouseDelta();

    bool SetRelativeMouse(bool enabled);

    // Touch (Android). Fed from SDL_FINGER* events; coordinates are normalized [0,1].
    void OnFingerDown(long long fingerId, float x, float y);
    void OnFingerUp(long long fingerId, float x, float y);
    void OnFingerMotion(long long fingerId, float x, float y, float dx, float dy);
    TouchState ConsumeTouchState();
    int GetTouchFingerCount();
    void SetMouseCapturedByUi(bool captured);
    void SetKeyboardCapturedByUi(bool captured);
    bool IsMouseCapturedByUi();
    bool IsKeyboardCapturedByUi();
} // namespace pe::InputState
