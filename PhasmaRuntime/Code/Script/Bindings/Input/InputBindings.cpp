#include "Script/ScriptSystem.h"
#include "API/RHI.h"
#include "Script/Bindings/Input/InputState.h"
#include "Script/ScriptRuntimeHooks.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <SDL_system.h>
namespace
{
    // Vibrate the device via the Android Vibrator service (JNI). minSdk is 26, so
    // VibrationEffect.createOneShot is always available. Best-effort: any JNI
    // failure is cleared so a missing permission / OEM quirk never crashes the game.
    // Requires the VIBRATE permission in AndroidManifest.xml.
    void PlatformVibrate(int ms)
    {
        JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        if (!env)
            return;
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!activity || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID getService =
            env->GetMethodID(activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring name = env->NewStringUTF("vibrator");
        jobject vibrator = env->CallObjectMethod(activity, getService, name);
        if (vibrator && !env->ExceptionCheck())
        {
            jclass effectClass = env->FindClass("android/os/VibrationEffect");
            jmethodID createOneShot =
                env->GetStaticMethodID(effectClass, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
            // -1 == VibrationEffect.DEFAULT_AMPLITUDE
            jobject effect = env->CallStaticObjectMethod(effectClass, createOneShot, static_cast<jlong>(ms), -1);
            jclass vibratorClass = env->GetObjectClass(vibrator);
            jmethodID vibrate = env->GetMethodID(vibratorClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
            env->CallVoidMethod(vibrator, vibrate, effect);
            env->DeleteLocalRef(effect);
            env->DeleteLocalRef(effectClass);
            env->DeleteLocalRef(vibratorClass);
            env->DeleteLocalRef(vibrator);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
    }
} // namespace
#endif

namespace pe
{
    namespace InputState
    {
        namespace
        {
            MouseDelta s_mouseDelta{};
            MouseDelta s_mouseDeltaFrame{};
            MouseDelta s_mouseWheel{};
            bool s_relativeMouseRequested = false;
            bool s_softwareMouseDelta = false;
            bool s_softwareMouseDeltaFrameValid = false;
            bool s_softwareMouseDeltaConsumed = false;
            bool s_mousePositionInitialized = false;
            bool s_mouseCapturedByUi = false;
            bool s_keyboardCapturedByUi = false;
            int s_lastMouseX = 0;
            int s_lastMouseY = 0;
            int s_softwareMouseDeltaFrameX = 0;
            int s_softwareMouseDeltaFrameY = 0;
            MouseDelta s_softwareMouseDeltaFrame{};

            // Touch tracking (Android). SDL finger coords are normalized [0,1] in the window.
            struct Finger
            {
                long long id = 0;
                float x = 0.0f;
                float y = 0.0f;
                bool active = false;
            };
            std::array<Finger, 2> s_fingers{};
            int s_fingerCount = 0;
            float s_touchDx = 0.0f;
            float s_touchDy = 0.0f;
            float s_touchPinch = 0.0f;
            float s_prevPinchDist = 0.0f;

            int FindFinger(long long id)
            {
                for (int i = 0; i < static_cast<int>(s_fingers.size()); ++i)
                    if (s_fingers[i].active && s_fingers[i].id == id)
                        return i;
                return -1;
            }

            float PinchDistance()
            {
                if (!s_fingers[0].active || !s_fingers[1].active)
                    return 0.0f;
                const float dx = s_fingers[0].x - s_fingers[1].x;
                const float dy = s_fingers[0].y - s_fingers[1].y;
                return std::sqrt(dx * dx + dy * dy);
            }

            bool IsWslEnvironment()
            {
                static const bool isWsl = []()
                {
                    if (std::getenv("WSL_DISTRO_NAME") || std::getenv("WSL_INTEROP"))
                        return true;

                    std::ifstream osRelease("/proc/sys/kernel/osrelease");
                    std::string text;
                    std::getline(osRelease, text);
                    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
                                   { return static_cast<char>(std::tolower(c)); });
                    return text.find("microsoft") != std::string::npos || text.find("wsl") != std::string::npos;
                }();
                return isWsl;
            }

            void ResetAbsoluteMouseDelta()
            {
                SDL_GetMouseState(&s_lastMouseX, &s_lastMouseY);
                s_mousePositionInitialized = true;
                s_softwareMouseDeltaFrame = {};
                s_softwareMouseDeltaFrameX = s_lastMouseX;
                s_softwareMouseDeltaFrameY = s_lastMouseY;
                s_softwareMouseDeltaFrameValid = false;
                s_softwareMouseDeltaConsumed = false;
            }

            MouseDelta ReadSoftwareMouseDeltaFrame()
            {
                if (s_mouseCapturedByUi)
                    return {};

                if (!s_softwareMouseDeltaFrameValid)
                {
                    int x = 0;
                    int y = 0;
                    SDL_GetMouseState(&x, &y);
                    s_softwareMouseDeltaFrameX = x;
                    s_softwareMouseDeltaFrameY = y;
                    s_softwareMouseDeltaFrameValid = true;

                    if (!s_mousePositionInitialized)
                    {
                        s_lastMouseX = x;
                        s_lastMouseY = y;
                        s_mousePositionInitialized = true;
                        s_softwareMouseDeltaFrame = {};
                    }
                    else
                    {
                        s_softwareMouseDeltaFrame.x = std::clamp(x - s_lastMouseX, -128, 128);
                        s_softwareMouseDeltaFrame.y = std::clamp(y - s_lastMouseY, -128, 128);
                    }
                }

                return s_softwareMouseDeltaFrame;
            }
        } // namespace

        void BeginFrame()
        {
            s_mouseDelta = {};
            s_mouseDeltaFrame = {};
            s_mouseWheel = {};
            s_mouseCapturedByUi = false;
            s_keyboardCapturedByUi = false;
            s_softwareMouseDeltaFrame = {};
            s_softwareMouseDeltaFrameValid = false;
            s_softwareMouseDeltaConsumed = false;
            // Per-frame touch motion/pinch reset; finger down-state persists across frames.
            s_touchDx = 0.0f;
            s_touchDy = 0.0f;
            s_touchPinch = 0.0f;
        }

        void AddMouseMotion(int xrel, int yrel)
        {
            s_mouseDelta.x += xrel;
            s_mouseDelta.y += yrel;
            s_mouseDeltaFrame.x += xrel;
            s_mouseDeltaFrame.y += yrel;
        }

        void AddMouseWheel(int x, int y)
        {
            s_mouseWheel.x += x;
            s_mouseWheel.y += y;
        }

        void OnFingerDown(long long fingerId, float x, float y)
        {
            if (FindFinger(fingerId) >= 0)
                return;
            for (auto &f : s_fingers)
            {
                if (!f.active)
                {
                    f = Finger{fingerId, x, y, true};
                    break;
                }
            }
            s_fingerCount = std::min<int>(s_fingerCount + 1, static_cast<int>(s_fingers.size()));
            s_prevPinchDist = PinchDistance(); // reset pinch baseline when the 2nd finger lands
        }

        void OnFingerUp(long long fingerId, float, float)
        {
            const int i = FindFinger(fingerId);
            if (i >= 0)
                s_fingers[i].active = false;
            s_fingerCount = std::max(0, s_fingerCount - 1);
            s_prevPinchDist = 0.0f;
        }

        void OnFingerMotion(long long fingerId, float x, float y, float dx, float dy)
        {
            const int i = FindFinger(fingerId);
            if (i < 0)
                return;
            s_fingers[i].x = x;
            s_fingers[i].y = y;

            if (s_fingerCount >= 2)
            {
                // Two fingers: pinch-to-zoom; ignore per-finger pan to avoid fighting the look.
                const float dist = PinchDistance();
                if (s_prevPinchDist > 0.0f)
                    s_touchPinch += dist - s_prevPinchDist;
                s_prevPinchDist = dist;
            }
            else if (i == 0)
            {
                // One finger: accumulate primary-finger drag (normalized units).
                s_touchDx += dx;
                s_touchDy += dy;
            }
        }

        MouseDelta ConsumeMouseDelta()
        {
            if (s_mouseCapturedByUi)
                return {};

            if (s_softwareMouseDelta)
            {
                if (s_softwareMouseDeltaConsumed)
                    return {};
                const MouseDelta delta = ReadSoftwareMouseDeltaFrame();
                s_lastMouseX = s_softwareMouseDeltaFrameX;
                s_lastMouseY = s_softwareMouseDeltaFrameY;
                s_softwareMouseDeltaConsumed = true;
                return delta;
            }

            MouseDelta delta = s_mouseDelta;
            s_mouseDelta = {};
            return delta;
        }

        MouseDelta PeekMouseDelta()
        {
            if (s_mouseCapturedByUi)
                return {};

            if (s_softwareMouseDelta)
                return ReadSoftwareMouseDeltaFrame();

            return s_mouseDeltaFrame;
        }

        MouseDelta GetMouseWheel()
        {
            return s_mouseCapturedByUi ? MouseDelta{} : s_mouseWheel;
        }

        void ResetMouseDelta()
        {
            s_mouseDelta = {};
            s_mouseDeltaFrame = {};
            s_softwareMouseDeltaFrame = {};
            s_softwareMouseDeltaFrameValid = false;
            s_softwareMouseDeltaConsumed = false;
            int discardX = 0;
            int discardY = 0;
            SDL_GetRelativeMouseState(&discardX, &discardY);
            ResetAbsoluteMouseDelta();
        }

        TouchState ConsumeTouchState()
        {
            TouchState out{};
            if (!s_mouseCapturedByUi)
            {
                out.dx = s_touchDx;
                out.dy = s_touchDy;
                out.pinch = s_touchPinch;
                out.fingers = s_fingerCount;
            }
            s_touchDx = 0.0f;
            s_touchDy = 0.0f;
            s_touchPinch = 0.0f;
            return out;
        }

        int GetTouchFingerCount()
        {
            return s_fingerCount;
        }

        void SetRelativeMouseRequested(bool enabled)
        {
            s_relativeMouseRequested = enabled;
        }

        bool IsRelativeMouseRequested()
        {
            return s_relativeMouseRequested;
        }

        bool SetRelativeMouse(bool enabled)
        {
            ResetMouseDelta();
            SetRelativeMouseRequested(enabled);

            if (!enabled)
            {
                s_softwareMouseDelta = false;
                if (SDL_GetRelativeMouseMode() == SDL_TRUE)
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                return true;
            }

            if (IsWslEnvironment())
            {
                static bool warned = false;
                if (!warned)
                {
                    PE_WARN("[Input] WSL detected; using software mouse delta fallback instead of SDL relative mouse mode");
                    warned = true;
                }
                s_softwareMouseDelta = true;
                return true;
            }

            const int result = SDL_SetRelativeMouseMode(SDL_TRUE);
            static bool warned = false;
            if (result != 0 && !warned)
            {
                PE_WARN("[Input] SDL_SetRelativeMouseMode failed: %s", SDL_GetError());
                warned = true;
            }

            if (result != 0)
                s_softwareMouseDelta = true;
            return result == 0 && SDL_GetRelativeMouseMode() == SDL_TRUE;
        }

        // Hosts (editor Window / PlayerHost) write the full capture state once per
        // frame after event polling; plain assignment keeps the flags in sync. An
        // OR-latch here sticks forever after a single captured frame and permanently
        // disables input.* for scripts (dead fly camera).
        void SetMouseCapturedByUi(bool captured)
        {
            s_mouseCapturedByUi = captured;
        }

        void SetKeyboardCapturedByUi(bool captured)
        {
            s_keyboardCapturedByUi = captured;
        }

        bool IsMouseCapturedByUi()
        {
            return s_mouseCapturedByUi;
        }

        bool IsKeyboardCapturedByUi()
        {
            return s_keyboardCapturedByUi;
        }
    } // namespace InputState

    static struct InputBindings
    {
        InputBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table input = lua.create_named_table("input");

                // Keyboard state
                input.set_function("is_key_down", [](const std::string &keyName) -> bool {
                    if (InputState::IsKeyboardCapturedByUi()) return false;
                    SDL_Scancode sc = SDL_GetScancodeFromName(keyName.c_str());
                    if (sc == SDL_SCANCODE_UNKNOWN) return false;
                    const Uint8 *state = SDL_GetKeyboardState(nullptr);
                    return state[sc] != 0;
                });

                input.set_function("is_key_pressed", [](const std::string &keyName) -> bool {
                    if (InputState::IsKeyboardCapturedByUi()) return false;
                    SDL_Keycode kc = SDL_GetKeyFromName(keyName.c_str());
                    if (kc == SDLK_UNKNOWN) return false;
                    SDL_Scancode sc = SDL_GetScancodeFromKey(kc);
                    const Uint8 *state = SDL_GetKeyboardState(nullptr);
                    return state[sc] != 0;
                });

                // Mouse state
                input.set_function("get_mouse_position", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    int x, y;
                    if (InputState::IsMouseCapturedByUi())
                    {
                        x = 0;
                        y = 0;
                    }
                    else
                    {
                        SDL_GetMouseState(&x, &y);
                    }
                    sol::table t = lua.create_table();
                    t["x"] = x;
                    t["y"] = y;
                    return t;
                });

                input.set_function("get_mouse_delta", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    InputState::MouseDelta delta = InputState::ConsumeMouseDelta();

                    sol::table t = lua.create_table();
                    t["x"] = delta.x;
                    t["y"] = delta.y;
                    return t;
                });

                input.set_function("peek_mouse_delta", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    InputState::MouseDelta delta = InputState::PeekMouseDelta();

                    sol::table t = lua.create_table();
                    t["x"] = delta.x;
                    t["y"] = delta.y;
                    return t;
                });

                // Touch (Android / touchscreens). One-finger drag -> dx/dy (normalized fraction of the
                // surface); two-finger -> pinch (normalized). fingers = number currently down.
                input.set_function("get_touch", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    InputState::TouchState t = InputState::ConsumeTouchState();
                    sol::table out = lua.create_table();
                    out["dx"] = t.dx;
                    out["dy"] = t.dy;
                    out["pinch"] = t.pinch;
                    out["fingers"] = t.fingers;
                    return out;
                });

                input.set_function("touch_fingers", []() -> int {
                    return InputState::GetTouchFingerCount();
                });

                input.set_function("get_mouse_wheel", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    InputState::MouseDelta wheel = InputState::GetMouseWheel();

                    sol::table t = lua.create_table();
                    t["x"] = wheel.x;
                    t["y"] = wheel.y;
                    return t;
                });

                input.set_function("is_mouse_down", [](int button) -> bool {
                    if (InputState::IsMouseCapturedByUi()) return false;
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(button)) != 0;
                });

                input.set_function("is_left_mouse_down", []() -> bool {
                    if (InputState::IsMouseCapturedByUi()) return false;
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
                });

                input.set_function("is_right_mouse_down", []() -> bool {
                    if (InputState::IsMouseCapturedByUi()) return false;
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
                });

                input.set_function("is_viewport_focused", []() -> bool {
                    return IsScriptViewportFocused();
                });

                input.set_function("is_middle_mouse_down", []() -> bool {
                    if (InputState::IsMouseCapturedByUi()) return false;
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
                });

                // Relative mouse mode (hides cursor, captures deltas - needed for FPS-style camera)
                input.set_function("set_relative_mouse", [](bool enabled) -> bool {
                    return InputState::SetRelativeMouse(enabled);
                });

                input.set_function("is_relative_mouse", []() -> bool {
                    return InputState::IsRelativeMouseRequested() || SDL_GetRelativeMouseMode() == SDL_TRUE;
                });

                input.set_function("reset_mouse_delta", []() {
                    InputState::ResetMouseDelta();
                });

                // Warp mouse to center of window (useful when exiting relative mode)
                input.set_function("warp_mouse_center", []() {
                    SDL_Window *window = RHII.GetWindow();
                    if (window)
                    {
                        int w, h;
                        SDL_GetWindowSize(window, &w, &h);
                        SDL_WarpMouseInWindow(window, w / 2, h / 2);
                    }
                });

                // Haptic pulse (Android Vibrator via JNI); no-op on desktop. ms = duration.
                input.set_function("vibrate", [](int ms) {
#if defined(__ANDROID__)
                    PlatformVibrate(ms > 0 ? ms : 12);
#else
                    (void)ms;
#endif
                }); });
        }
    } s_inputBindings;
} // namespace pe
