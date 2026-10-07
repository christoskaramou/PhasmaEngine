#include "Script/ScriptSystem.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/Bindings/Settings/SettingsBindings.h"
#include "API/RHI.h"

namespace pe
{
    static struct RuntimeEngineBindings
    {
        RuntimeEngineBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table engine = lua["engine"].get_or_create<sol::table>();

                engine.set_function("get_metrics", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    double dt = FrameTimer::Instance().GetDelta();
                    t["fps"] = dt > 0.0 ? 1.0 / dt : 0.0;
                    t["delta_ms"] = dt * 1000.0;
                    return t;
                });

                // Unfocused windows render at 30 FPS unless this is off (on by default; not saved).
                engine.set_function("set_background_fps_cap", [](bool enabled) {
                    FrameTimer::Instance().SetBackgroundCap(enabled);
                });
                engine.set_function("get_background_fps_cap", []() -> bool {
                    return FrameTimer::Instance().GetBackgroundCap();
                });

                engine.set_function("compile_shaders", []() {
                    EventSystem::PushEvent(EventType::CompileShaders);
                });

                engine.set_function("is_play_mode", []() -> bool {
                    return IsScriptPlayMode();
                });

                engine.set_function("set_play_mode", [](bool enabled) {
                    SetScriptPlayMode(enabled);
                });

                engine.set_function("is_paused", []() -> bool {
                    return IsScriptPaused();
                });

                engine.set_function("set_paused", [](bool paused) {
                    SetScriptPaused(paused);
                });

                // C++ script module state. error is the latest rejection; attempts counts every artifact tried (so a
                // repeated identical rejection is visible); artifact/attempted identify build outputs as "<size>:<mtime>".
                engine.set_function("native_scripts_status", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    CppScriptStatus status;
                    if (auto *scripts = GetGlobalSystem<ScriptSystem>())
                        status = scripts->CppScriptStatusSnapshot();
                    sol::table t = lua.create_table();
                    t["reloads"] = status.reloads;
                    t["active"] = status.active;
                    t["scripts"] = status.scriptCount;
                    t["error"] = status.error;
                    t["attempts"] = status.attempts;
                    t["artifact"] = status.activeArtifact;
                    t["attempted"] = status.attemptedArtifact;
                    t["live_reload"] = status.liveReload;
                    return t;
                });

                engine.set_function("quit", []() {
                    EventSystem::PushEvent(EventType::Quit);
                });

                engine.set_function("take_screenshot", [](sol::optional<std::string> filename) {
                    EventSystem::PushEvent(EventType::Screenshot, filename.value_or(""));
                });

                engine.set_function("get_window_size", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    int w = 0;
                    int h = 0;
                    GetWindowSize(w, h);
                    t["w"] = w;
                    t["h"] = h;
                    return t;
                });

                engine.set_function("get_viewport_rect", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    int w = 0;
                    int h = 0;
                    SDL_Window *window = RHII.GetWindow();
                    if (window)
                        SDL_GetWindowSize(window, &w, &h);
                    t["x"] = 0.0f;
                    t["y"] = 0.0f;
                    t["w"] = w;
                    t["h"] = h;
                    t["valid"] = w > 0 && h > 0;
                    return t;
                });

                engine.set_function("get_window_mode", []() -> std::string {
                    return GetWindowModeToken();
                });

                engine.set_function("set_window_mode", [](const std::string &mode) {
                    SetWindowModeToken(mode);
                }); });
        }
    } s_runtimeEngineBindings;
} // namespace pe
