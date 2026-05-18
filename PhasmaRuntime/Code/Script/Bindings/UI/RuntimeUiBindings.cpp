#include "Script/ScriptSystem.h"
#include "UI/RuntimeUi.h"

namespace pe
{
    namespace
    {
        RuntimeUiSystem *RequireRuntimeUi()
        {
            RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
            if (!runtimeUi)
                PE_WARN("[Lua] runtime_ui: no active runtime UI system");
            return runtimeUi;
        }

        struct RuntimeUiBindingRegistrar
        {
            RuntimeUiBindingRegistrar()
            {
                ScriptSystem::AddBindings(
                    [](sol::state &lua)
                    {
                        sol::table ui = lua.create_named_table("runtime_ui");

                        ui.set_function("set_visible", [](const std::string &screenId, bool visible)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenVisible(screenId, visible); });
                        ui.set_function("show", [](const std::string &screenId)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenVisible(screenId, true); });
                        ui.set_function("hide", [](const std::string &screenId)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenVisible(screenId, false); });
                        ui.set_function("is_visible", [](const std::string &screenId) -> bool
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                return runtimeUi->IsScreenVisible(screenId);
                                            return false; });
                        ui.set_function("set_title", [](const std::string &screenId, const std::string &title)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenTitle(screenId, title); });
                        ui.set_function("clear", [](const std::string &screenId)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->ClearScreen(screenId); });
                        ui.set_function("remove", [](const std::string &screenId, const std::string &widgetId)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->RemoveWidget(screenId, widgetId); });
                        ui.set_function("set_text",
                                        [](const std::string &screenId,
                                           const std::string &widgetId,
                                           const std::string &label,
                                           const std::string &value)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetText(screenId, widgetId, label, value);
                                        });
                        ui.set_function("set_number",
                                        [](const std::string &screenId,
                                           const std::string &widgetId,
                                           const std::string &label,
                                           double value)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetNumber(screenId, widgetId, label, value);
                                        });
                        ui.set_function("set_bool",
                                        [](const std::string &screenId,
                                           const std::string &widgetId,
                                           const std::string &label,
                                           bool value)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetBool(screenId, widgetId, label, value);
                                        });
                        ui.set_function("get_bool",
                                        [](const std::string &screenId,
                                           const std::string &widgetId,
                                           sol::optional<bool> fallback) -> bool
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                return runtimeUi->GetBool(screenId, widgetId, fallback.value_or(false));
                                            return fallback.value_or(false);
                                        });
                        ui.set_function("set_button",
                                        [](const std::string &screenId,
                                           const std::string &widgetId,
                                           const std::string &label)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetButton(screenId, widgetId, label);
                                        });
                        ui.set_function("consume_click",
                                        [](const std::string &screenId, const std::string &widgetId) -> bool
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                return runtimeUi->ConsumeButtonClick(screenId, widgetId);
                                            return false;
                                        });
                    });
            }
        };

        RuntimeUiBindingRegistrar s_runtimeUiBindingRegistrar;
    } // namespace
} // namespace pe
