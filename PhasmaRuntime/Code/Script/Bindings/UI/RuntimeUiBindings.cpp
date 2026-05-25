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

        float ReadFloatOption(const sol::table &table, const char *key, float fallback)
        {
            sol::object value = table[key];
            if (value.is<double>())
                return static_cast<float>(value.as<double>());
            if (value.is<int>())
                return static_cast<float>(value.as<int>());
            return fallback;
        }

        float ReadFloatOption(const sol::table &table, int key, float fallback)
        {
            sol::object value = table[key];
            if (value.is<double>())
                return static_cast<float>(value.as<double>());
            if (value.is<int>())
                return static_cast<float>(value.as<int>());
            return fallback;
        }

        std::string ReadStringOption(const sol::table &table, const char *key, const std::string &fallback = "")
        {
            sol::object value = table[key];
            if (value.is<std::string>())
                return value.as<std::string>();
            return fallback;
        }

        struct ImageOptions
        {
            std::string path;
            std::string label;
            float width = 0.0f;
            float height = 0.0f;
        };

        ImageOptions ReadImageOptions(const sol::table &options)
        {
            ImageOptions result{};
            result.path = ReadStringOption(options, "path");
            if (result.path.empty())
                result.path = ReadStringOption(options, "resource");
            result.label = ReadStringOption(options, "label");
            result.width = ReadFloatOption(options, "width", 0.0f);
            result.height = ReadFloatOption(options, "height", 0.0f);

            sol::object sizeObject = options["size"];
            if (sizeObject.is<sol::table>())
            {
                sol::table size = sizeObject.as<sol::table>();
                result.width = ReadFloatOption(size, "width", result.width);
                result.height = ReadFloatOption(size, "height", result.height);
                result.width = ReadFloatOption(size, "x", result.width);
                result.height = ReadFloatOption(size, "y", result.height);
                result.width = ReadFloatOption(size, "1", result.width);
                result.height = ReadFloatOption(size, "2", result.height);
                result.width = ReadFloatOption(size, 1, result.width);
                result.height = ReadFloatOption(size, 2, result.height);
            }

            return result;
        }

        void SetRuntimeUiImage(const std::string &screenId, const std::string &widgetId, const ImageOptions &options)
        {
            if (options.path.empty())
            {
                PE_WARN("[Lua] runtime_ui.set_image requires a path");
                return;
            }

            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                runtimeUi->SetImage(screenId, widgetId, options.label, options.path, options.width, options.height);
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
                        ui.set_function("set_image",
                                        sol::overload(
                                            [](const std::string &screenId,
                                               const std::string &widgetId,
                                               const std::string &path)
                                            {
                                                ImageOptions options{};
                                                options.path = path;
                                                SetRuntimeUiImage(screenId, widgetId, options);
                                            },
                                            [](const std::string &screenId,
                                               const std::string &widgetId,
                                               const std::string &path,
                                               double width,
                                               double height)
                                            {
                                                ImageOptions options{};
                                                options.path = path;
                                                options.width = static_cast<float>(width);
                                                options.height = static_cast<float>(height);
                                                SetRuntimeUiImage(screenId, widgetId, options);
                                            },
                                            [](const std::string &screenId,
                                               const std::string &widgetId,
                                               const std::string &label,
                                               const std::string &path,
                                               double width,
                                               double height)
                                            {
                                                ImageOptions options{};
                                                options.path = path;
                                                options.label = label;
                                                options.width = static_cast<float>(width);
                                                options.height = static_cast<float>(height);
                                                SetRuntimeUiImage(screenId, widgetId, options);
                                            },
                                            [](const std::string &screenId,
                                               const std::string &widgetId,
                                               const std::string &path,
                                               const sol::table &optionsTable)
                                            {
                                                ImageOptions options = ReadImageOptions(optionsTable);
                                                options.path = path;
                                                SetRuntimeUiImage(screenId, widgetId, options);
                                            },
                                            [](const std::string &screenId,
                                               const std::string &widgetId,
                                               const sol::table &optionsTable)
                                            {
                                                SetRuntimeUiImage(screenId, widgetId, ReadImageOptions(optionsTable));
                                            }));
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
