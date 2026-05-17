#include "Script/ScriptSystem.h"
#include "Script/ScriptRuntimeHooks.h"
#include "GUI/GUIState.h"
#include "GUI/GUI.h"
#include "GUI/Widgets/Console.h"
#include "GUI/Widgets/FileSelector.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace pe
{
    namespace
    {
        bool HasImGuiContext()
        {
            return ImGui::GetCurrentContext() != nullptr;
        }
    } // namespace

    static struct EngineBindings
    {
        EngineBindings()
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

                engine.set_function("compile_shaders", []() {
                    EventSystem::PushEvent(EventType::CompileShaders);
                });

                // Play mode
                engine.set_function("is_play_mode", []() -> bool {
                    return IsScriptPlayMode();
                });

                engine.set_function("set_play_mode", [](bool enabled) {
                    SetScriptPlayMode(enabled);
                });

                // Pause
                engine.set_function("is_paused", []() -> bool {
                    return IsScriptPaused();
                });

                engine.set_function("set_paused", [](bool paused) {
                    SetScriptPaused(paused);
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

                engine.set_function("reload_module", []() {
                    EventSystem::PushEvent(EventType::ReloadModule);
                });

                engine.set_function("is_popup_open", []() -> bool {
                    if (!HasImGuiContext())
                        return false;
                    return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
                });

                engine.set_function("want_capture_keyboard", []() -> bool {
                    if (!HasImGuiContext())
                        return false;
                    return ImGui::GetIO().WantCaptureKeyboard;
                });

                // Returns the name of the currently focused ImGui window, or "" if none.
                engine.set_function("get_focused_window", []() -> std::string {
                    ImGuiWindow *w = HasImGuiContext() ? ImGui::GetCurrentContext()->NavWindow : nullptr;
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
                    if (!HasImGuiContext())
                        return;
                    ImGui::SetKeyboardFocusHere(offset);
                });

                engine.set_function("take_screenshot", [](sol::optional<std::string> filename) {
                    std::string path = filename.value_or("");
                    EventSystem::PushEvent(EventType::Screenshot, path);
                });

                // Returns recent console log entries as a Lua array of {level, text} tables.
                // Optional count (default 100) limits how many recent entries to return.
                // Optional level filters by "info", "warn", or "error".
                engine.set_function("get_console_log", [](sol::optional<int> count, sol::optional<std::string> level, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return result;
                    auto *console = r->GetGUI().GetWidget<Console>();
                    if (!console) return result;

                    const auto &logs = console->GetLogs();
                    int n = count.value_or(100);
                    std::string lvl = level.value_or("all");
                    int start = std::max(0, (int)logs.size() - n);
                    int idx = 1;
                    for (int i = start; i < (int)logs.size(); ++i)
                    {
                        const auto &e = logs[i];
                        std::string typeStr = e.type == LogType::Warn  ? "warn"
                                           : e.type == LogType::Error ? "error"
                                                                      : "info";
                        if (lvl != "all" && typeStr != lvl)
                            continue;
                        sol::table entry = lua.create_table();
                        entry["level"] = typeStr;
                        entry["text"]  = e.text;
                        result[idx++]  = entry;
                    }
                    return result;
                }); });
        }
    } s_engineBindings;
} // namespace pe
