#include "Script/ScriptSystem.h"
#include "API/RHI.h"
#include "Script/Bindings/Input/InputState.h"
#include "Script/ScriptRuntimeHooks.h"

namespace pe
{
    namespace InputState
    {
        namespace
        {
            MouseDelta s_mouseDelta{};
            bool s_relativeMouseRequested = false;
            bool s_softwareMouseDelta = false;
            bool s_mousePositionInitialized = false;
            bool s_mouseCapturedByUi = false;
            bool s_keyboardCapturedByUi = false;
            int s_lastMouseX = 0;
            int s_lastMouseY = 0;

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
            }
        } // namespace

        void BeginFrame()
        {
            s_mouseDelta = {};
            s_mouseCapturedByUi = false;
            s_keyboardCapturedByUi = false;
        }

        void AddMouseMotion(int xrel, int yrel)
        {
            s_mouseDelta.x += xrel;
            s_mouseDelta.y += yrel;
        }

        MouseDelta ConsumeMouseDelta()
        {
            if (s_mouseCapturedByUi)
                return {};

            if (s_softwareMouseDelta)
            {
                int x = 0;
                int y = 0;
                SDL_GetMouseState(&x, &y);
                if (!s_mousePositionInitialized)
                {
                    s_lastMouseX = x;
                    s_lastMouseY = y;
                    s_mousePositionInitialized = true;
                    return {};
                }

                MouseDelta delta{};
                delta.x = std::clamp(x - s_lastMouseX, -128, 128);
                delta.y = std::clamp(y - s_lastMouseY, -128, 128);
                s_lastMouseX = x;
                s_lastMouseY = y;
                return delta;
            }

            MouseDelta delta = s_mouseDelta;
            s_mouseDelta = {};
            return delta;
        }

        void ResetMouseDelta()
        {
            s_mouseDelta = {};
            int discardX = 0;
            int discardY = 0;
            SDL_GetRelativeMouseState(&discardX, &discardY);
            ResetAbsoluteMouseDelta();
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

        void SetMouseCapturedByUi(bool captured)
        {
            s_mouseCapturedByUi = s_mouseCapturedByUi || captured;
        }

        void SetKeyboardCapturedByUi(bool captured)
        {
            s_keyboardCapturedByUi = s_keyboardCapturedByUi || captured;
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
                }); });
        }
    } s_inputBindings;
} // namespace pe
