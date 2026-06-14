#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNodeHandle.h"
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

        bool ReadBoolOption(const sol::table &table, const char *key, bool fallback)
        {
            sol::object value = table[key];
            if (value.is<bool>())
                return value.as<bool>();
            return fallback;
        }

        std::string ReadStringOption(const sol::table &table, const char *key, const std::string &fallback = "")
        {
            sol::object value = table[key];
            if (value.is<std::string>())
                return value.as<std::string>();
            return fallback;
        }

        RuntimeUiColor ReadColorOption(const sol::table &table, const char *key, const RuntimeUiColor &fallback)
        {
            sol::object value = table[key];
            if (!value.is<sol::table>())
                return fallback;

            sol::table color = value.as<sol::table>();
            RuntimeUiColor result = fallback;
            result.r = ReadFloatOption(color, "r", result.r);
            result.g = ReadFloatOption(color, "g", result.g);
            result.b = ReadFloatOption(color, "b", result.b);
            result.a = ReadFloatOption(color, "a", result.a);
            result.r = ReadFloatOption(color, 1, result.r);
            result.g = ReadFloatOption(color, 2, result.g);
            result.b = ReadFloatOption(color, 3, result.b);
            result.a = ReadFloatOption(color, 4, result.a);
            return result;
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

        struct QuadOptions
        {
            RuntimeUiQuadDesc desc{};
            std::string path;
            std::string label;
            std::string title;
            std::string subtitle;
            std::string body;
            std::string footer;
        };

        void ReadPositionOptions(const sol::table &options, RuntimeUiQuadDesc &desc)
        {
            desc.x = ReadFloatOption(options, "x", desc.x);
            desc.y = ReadFloatOption(options, "y", desc.y);
            desc.z = ReadFloatOption(options, "z", desc.z);
            desc.width = ReadFloatOption(options, "width", desc.width);
            desc.height = ReadFloatOption(options, "height", desc.height);
            desc.width = ReadFloatOption(options, "w", desc.width);
            desc.height = ReadFloatOption(options, "h", desc.height);

            sol::object positionObject = options["position"];
            if (positionObject.is<sol::table>())
            {
                sol::table position = positionObject.as<sol::table>();
                desc.x = ReadFloatOption(position, "x", desc.x);
                desc.y = ReadFloatOption(position, "y", desc.y);
                desc.z = ReadFloatOption(position, "z", desc.z);
                desc.x = ReadFloatOption(position, 1, desc.x);
                desc.y = ReadFloatOption(position, 2, desc.y);
                desc.z = ReadFloatOption(position, 3, desc.z);
            }

            sol::object sizeObject = options["size"];
            if (sizeObject.is<sol::table>())
            {
                sol::table size = sizeObject.as<sol::table>();
                desc.width = ReadFloatOption(size, "width", desc.width);
                desc.height = ReadFloatOption(size, "height", desc.height);
                desc.width = ReadFloatOption(size, "x", desc.width);
                desc.height = ReadFloatOption(size, "y", desc.height);
                desc.width = ReadFloatOption(size, 1, desc.width);
                desc.height = ReadFloatOption(size, 2, desc.height);
            }
        }

        QuadOptions ReadQuadOptions(const sol::table &options)
        {
            QuadOptions result{};
            result.path = ReadStringOption(options, "path");
            if (result.path.empty())
                result.path = ReadStringOption(options, "resource");
            if (result.path.empty())
                result.path = ReadStringOption(options, "image");

            result.label = ReadStringOption(options, "label");
            result.title = ReadStringOption(options, "title");
            result.subtitle = ReadStringOption(options, "subtitle");
            result.body = ReadStringOption(options, "body");
            result.footer = ReadStringOption(options, "footer");
            ReadPositionOptions(options, result.desc);
            result.desc.fillColor = ReadColorOption(options, "fill", result.desc.fillColor);
            result.desc.borderColor = ReadColorOption(options, "border", result.desc.borderColor);
            result.desc.accentColor = ReadColorOption(options, "accent", result.desc.accentColor);
            result.desc.textColor = ReadColorOption(options, "text_color", result.desc.textColor);
            result.desc.imageTint = ReadColorOption(options, "image_tint", result.desc.imageTint);
            result.desc.draggable = ReadBoolOption(options, "draggable", result.desc.draggable);
            result.desc.selected = ReadBoolOption(options, "selected", result.desc.selected);
            result.desc.visible = ReadBoolOption(options, "visible", result.desc.visible);
            result.desc.bringToFront = ReadBoolOption(options, "bring_to_front", result.desc.bringToFront);
            result.desc.noInput = ReadBoolOption(options, "no_input", result.desc.noInput);
            result.desc.fontScale = ReadFloatOption(options, "font_scale", result.desc.fontScale);

            sol::object nodeObject = options["node"];
            if (nodeObject.is<SceneNodeHandle>())
            {
                SceneNodeHandle handle = nodeObject.as<SceneNodeHandle>();
                if (Scene *scene = GetActiveScene())
                {
                    if (handle.IsValid(*scene))
                    {
                        scene->AddComponentFlag(handle.nodeId, Component_RuntimeUi);
                        result.desc.node = handle.nodeId;
                    }
                }
            }
            return result;
        }

        sol::table WidgetStateToTable(sol::this_state ts, const RuntimeUiWidgetState &state)
        {
            sol::state_view lua(ts);
            sol::table result = lua.create_table();
            result["hovered"] = state.hovered;
            result["active"] = state.active;
            result["clicked"] = state.clicked;
            result["down"] = state.down;
            result["dragging"] = state.dragging;
            result["drag_started"] = state.dragStarted;
            result["drag_released"] = state.dragReleased;
            result["mouse_x"] = state.mouseX;
            result["mouse_y"] = state.mouseY;
            result["drag_delta_x"] = state.dragDeltaX;
            result["drag_delta_y"] = state.dragDeltaY;
            sol::table mouse = lua.create_table();
            mouse["x"] = state.mouseX;
            mouse["y"] = state.mouseY;
            result["mouse"] = mouse;
            sol::table dragDelta = lua.create_table();
            dragDelta["x"] = state.dragDeltaX;
            dragDelta["y"] = state.dragDeltaY;
            result["drag_delta"] = dragDelta;
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
                        ui.set_function("set_screen_overlay", [](const std::string &screenId, bool overlay)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenOverlay(screenId, overlay); });
                        ui.set_function("get_surface_size", [](sol::this_state ts) -> sol::table
                                        {
                                            sol::state_view lua(ts);
                                            sol::table result = lua.create_table();
                                            uint32_t width = 0;
                                            uint32_t height = 0;
                                            float uiScale = 1.0f;
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                            {
                                                runtimeUi->GetFrameSurfaceSize(width, height);
                                                uiScale = runtimeUi->GetFrameUiScale();
                                            }
                                            result["w"] = width;
                                            result["h"] = height;
                                            result["width"] = width;
                                            result["height"] = height;
                                            // DPI/density font scale (io.FontGlobalScale). 1.0 on ~96dpi
                                            // desktops; up to 4.0 on phones. Resolution-relative UIs divide
                                            // their font_scale by this to cancel the backend's font DPI bump.
                                            result["ui_scale"] = uiScale;
                                            result["valid"] = width > 0 && height > 0;
                                            return result; });
                        ui.set_function("set_quad",
                                        [](const std::string &screenId,
                                           const std::string &widgetId,
                                           const sol::table &optionsTable)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                            {
                                                QuadOptions options = ReadQuadOptions(optionsTable);
                                                options.desc.label = options.label.c_str();
                                                options.desc.title = options.title.c_str();
                                                options.desc.subtitle = options.subtitle.c_str();
                                                options.desc.body = options.body.c_str();
                                                options.desc.footer = options.footer.c_str();
                                                runtimeUi->SetQuad(screenId, widgetId, options.desc, options.path);
                                            }
                                        });
                        ui.set_function("get_state",
                                        [](sol::this_state ts,
                                           const std::string &screenId,
                                           const std::string &widgetId) -> sol::object
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                            {
                                                RuntimeUiWidgetState state{};
                                                if (runtimeUi->GetWidgetState(screenId, widgetId, state))
                                                    return sol::make_object(ts, WidgetStateToTable(ts, state));
                                            }
                                            return sol::make_object(ts, sol::nil);
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
