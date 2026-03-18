#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "GUI/GUIState.h"
#include "GUI/GUI.h"
#include "GUI/Widgets/FileSelector.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    static struct EngineBindings
    {
        EngineBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table engine = lua.create_named_table("engine");

                engine.set_function("get_metrics", [&lua]() -> sol::table {
                    sol::table t = lua.create_table();
                    double dt = FrameTimer::Instance().GetDelta();
                    t["fps"] = dt > 0.0 ? 1.0 / dt : 0.0;
                    t["delta_ms"] = dt * 1000.0;
                    return t;
                });

                engine.set_function("compile_shaders", []() {
                    EventSystem::PushEvent(EventType::CompileShaders);
                });

                // Play mode
                engine.set_function("is_play_mode", []() -> bool {
                    return GUIState::s_playMode;
                });

                engine.set_function("set_play_mode", [](bool enabled) {
                    GUIState::s_playMode = enabled;
                    if (!enabled)
                        GUIState::s_isPaused = false;
                });

                // Pause
                engine.set_function("is_paused", []() -> bool {
                    return GUIState::s_isPaused;
                });

                engine.set_function("set_paused", [](bool paused) {
                    GUIState::s_isPaused = paused;
                });

                // GUI
                engine.set_function("toggle_gui", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->ToggleGUI();
                });

                engine.set_function("trigger_exit_confirmation", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetGUI().TriggerExitConfirmation();
                });

                engine.set_function("quit", []() {
                    EventSystem::PushEvent(EventType::Quit);
                });

                engine.set_function("is_popup_open", []() -> bool {
                    return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                });

                engine.set_function("want_capture_keyboard", []() -> bool {
                    return ImGui::GetIO().WantCaptureKeyboard;
                });

                // Returns the name of the currently focused ImGui window, or "" if none.
                engine.set_function("get_focused_window", []() -> std::string {
                    ImGuiWindow *w = ImGui::GetCurrentContext() ? ImGui::GetCurrentContext()->NavWindow : nullptr;
                    return (w && w->Name) ? std::string(w->Name) : std::string();
                });

                // Close the file selector (e.g. when ESC pressed while it's focused).
                engine.set_function("close_file_selector", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    auto *fs = r->GetGUI().GetWidget<FileSelector>();
                    if (fs) fs->CancelSelection();
                });

                // imgui table: thin wrappers for ImGui calls useful in scripts
                sol::table imgui = lua.create_named_table("imgui");

                // Pass -1 to clear keyboard focus (equivalent to ImGui::SetKeyboardFocusHere(-1))
                imgui.set_function("set_keyboard_focus_here", [](int offset) {
                    ImGui::SetKeyboardFocusHere(offset);
                });

                engine.set_function("take_screenshot", [](sol::optional<std::string> filename) {
                    std::string path = filename.value_or("");
                    EventSystem::PushEvent(EventType::Screenshot, path);
                }); });
        }
    } s_engineBindings;
} // namespace pe
#endif
