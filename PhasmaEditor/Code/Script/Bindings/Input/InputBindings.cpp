#include "Script/ScriptSystem.h"
#include "GUI/GUIState.h"
#include "API/RHI.h"

namespace pe
{
    static struct InputBindings
    {
        InputBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table input = lua.create_named_table("input");

                // Keyboard state
                input.set_function("is_key_down", [](const std::string &keyName) -> bool {
                    SDL_Scancode sc = SDL_GetScancodeFromName(keyName.c_str());
                    if (sc == SDL_SCANCODE_UNKNOWN) return false;
                    const Uint8 *state = SDL_GetKeyboardState(nullptr);
                    return state[sc] != 0;
                });

                input.set_function("is_key_pressed", [](const std::string &keyName) -> bool {
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
                    SDL_GetMouseState(&x, &y);
                    sol::table t = lua.create_table();
                    t["x"] = x;
                    t["y"] = y;
                    return t;
                });

                input.set_function("get_mouse_delta", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    int dx, dy;
                    SDL_GetRelativeMouseState(&dx, &dy);
                    sol::table t = lua.create_table();
                    t["x"] = dx;
                    t["y"] = dy;
                    return t;
                });

                input.set_function("is_mouse_down", [](int button) -> bool {
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(button)) != 0;
                });

                input.set_function("is_left_mouse_down", []() -> bool {
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
                });

                input.set_function("is_right_mouse_down", []() -> bool {
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
                });

                input.set_function("is_viewport_focused", []() -> bool {
                    return GUIState::s_sceneViewFocused;
                });

                input.set_function("is_middle_mouse_down", []() -> bool {
                    Uint32 state = SDL_GetMouseState(nullptr, nullptr);
                    return (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
                });

                // Relative mouse mode (hides cursor, captures deltas - needed for FPS-style camera)
                input.set_function("set_relative_mouse", [](bool enabled) {
                    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
                });

                input.set_function("is_relative_mouse", []() -> bool {
                    return SDL_GetRelativeMouseMode() == SDL_TRUE;
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
