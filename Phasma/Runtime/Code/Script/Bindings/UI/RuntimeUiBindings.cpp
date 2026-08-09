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

        bool TryParseVisualStyle(const std::string &value, RuntimeUiQuadVisualStyle &style)
        {
            if (value == "card")
                style = RuntimeUiQuadVisualStyle::Card;
            else if (value == "panel")
                style = RuntimeUiQuadVisualStyle::Panel;
            else if (value == "text")
                style = RuntimeUiQuadVisualStyle::Text;
            else if (value == "button")
                style = RuntimeUiQuadVisualStyle::Button;
            else if (value == "image")
                style = RuntimeUiQuadVisualStyle::Image;
            else
                return false;
            return true;
        }

        RuntimeUiColor ReadColor(const sol::table &color, const RuntimeUiColor &fallback)
        {
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

        RuntimeUiColor ReadColorOption(const sol::table &table, const char *key, const RuntimeUiColor &fallback)
        {
            sol::object value = table[key];
            if (!value.is<sol::table>())
                return fallback;
            return ReadColor(value.as<sol::table>(), fallback);
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
            result.desc.backgroundImageTint =
                ReadColorOption(options, "background_image_tint", result.desc.backgroundImageTint);
            result.desc.imageColorize = ReadBoolOption(options, "image_colorize", result.desc.imageColorize);
            result.desc.useBackgroundTint =
                ReadBoolOption(options, "use_background_tint", result.desc.useBackgroundTint);
            result.desc.imageWhiten = ReadFloatOption(options, "image_whiten", result.desc.imageWhiten);
            result.desc.draggable = ReadBoolOption(options, "draggable", result.desc.draggable);
            result.desc.selected = ReadBoolOption(options, "selected", result.desc.selected);
            result.desc.visible = ReadBoolOption(options, "visible", result.desc.visible);
            result.desc.bringToFront = ReadBoolOption(options, "bring_to_front", result.desc.bringToFront);
            result.desc.noInput = ReadBoolOption(options, "no_input", result.desc.noInput);
            result.desc.fontScale = ReadFloatOption(options, "font_scale", result.desc.fontScale);
            result.desc.fit = ReadBoolOption(options, "fit", result.desc.fit);
            result.desc.fit = ReadBoolOption(options, "auto_size", result.desc.fit);

            auto parseAlignH = [](const sol::table &o, const char *k, RuntimeUiTextAlignH cur) -> RuntimeUiTextAlignH
            {
                sol::object v = o[k];
                if (v.is<std::string>())
                {
                    const std::string s = v.as<std::string>();
                    if (s == "left")
                        return RuntimeUiTextAlignH::Left;
                    if (s == "center" || s == "centre" || s == "middle")
                        return RuntimeUiTextAlignH::Center;
                    if (s == "right")
                        return RuntimeUiTextAlignH::Right;
                    return RuntimeUiTextAlignH::Default;
                }
                if (v.is<double>())
                    return static_cast<RuntimeUiTextAlignH>(static_cast<uint8_t>(v.as<double>()));
                return cur;
            };
            auto parseAlignV = [](const sol::table &o, const char *k, RuntimeUiTextAlignV cur) -> RuntimeUiTextAlignV
            {
                sol::object v = o[k];
                if (v.is<std::string>())
                {
                    const std::string s = v.as<std::string>();
                    if (s == "top")
                        return RuntimeUiTextAlignV::Top;
                    if (s == "middle" || s == "center" || s == "centre")
                        return RuntimeUiTextAlignV::Middle;
                    if (s == "bottom")
                        return RuntimeUiTextAlignV::Bottom;
                    return RuntimeUiTextAlignV::Default;
                }
                if (v.is<double>())
                    return static_cast<RuntimeUiTextAlignV>(static_cast<uint8_t>(v.as<double>()));
                return cur;
            };
            result.desc.textAlignH = parseAlignH(options, "align_h", result.desc.textAlignH);
            result.desc.textAlignV = parseAlignV(options, "align_v", result.desc.textAlignV);
            result.desc.textOffsetX = ReadFloatOption(options, "offset_x", result.desc.textOffsetX);
            result.desc.textOffsetY = ReadFloatOption(options, "offset_y", result.desc.textOffsetY);
            result.desc.textInsetRight = ReadFloatOption(options, "text_inset_right", result.desc.textInsetRight);

            TryParseVisualStyle(ReadStringOption(options, "style"), result.desc.visualStyle);

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
            result["right_clicked"] = state.rightClicked;
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
                        ui.set_function("remove_many",
                                        [](const std::string &screenId, const sol::table &idTable) -> size_t
                                        {
                                            RuntimeUiSystem *runtimeUi = RequireRuntimeUi();
                                            if (!runtimeUi)
                                                return 0;
                                            std::vector<std::string> ids;
                                            ids.reserve(idTable.size());
                                            for (size_t i = 1; i <= idTable.size(); ++i)
                                            {
                                                if (auto id = idTable.raw_get<sol::optional<std::string>>(i))
                                                    ids.push_back(*id);
                                            }
                                            return runtimeUi->RemoveWidgets(screenId, ids);
                                        });
                        ui.set_function("set_many_visible",
                                        [](const std::string &screenId,
                                           const sol::table &idTable,
                                           bool visible) -> size_t
                                        {
                                            RuntimeUiSystem *runtimeUi = RequireRuntimeUi();
                                            if (!runtimeUi)
                                                return 0;
                                            std::vector<std::string> ids;
                                            ids.reserve(idTable.size());
                                            for (size_t i = 1; i <= idTable.size(); ++i)
                                            {
                                                if (auto id = idTable.raw_get<sol::optional<std::string>>(i))
                                                    ids.push_back(*id);
                                            }
                                            return runtimeUi->SetWidgetsVisible(screenId, ids, visible);
                                        });
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
                        ui.set_function("set_screen_scrollable", [](const std::string &screenId, bool scrollable)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenScrollable(screenId, scrollable); });
                        ui.set_function("set_screen_max_height", [](const std::string &screenId, double maxHeight)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetScreenMaxHeight(screenId, static_cast<float>(maxHeight)); });
                        // ImGui's wheel, not raw input: once the pointer is over an
                        // interactive quad the UI captures the event and raw input
                        // never sees it, so wheel-over-a-tile looked dead.
                        ui.set_function("get_wheel", []() -> double
                                        {
                            RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
                            return runtimeUi ? static_cast<double>(runtimeUi->MouseWheel()) : 0.0; });
                        // Script-drawn text entry (dev console) is not an ImGui input
                        // widget, so io.WantTextInput never fires for it. Setting this
                        // mutes every input.is_key_* reader in Lua at once; the caller
                        // reads its own keys with input.is_key_down_raw.
                        ui.set_function("set_keyboard_capture", [](bool captured)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetKeyboardCaptureOverride(captured); });
                        ui.set_function("set_text_scale", [](double scale)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetTextScale(static_cast<float>(scale)); });
                        ui.set_function("set_global_tint", [](const sol::table &tint)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetGlobalTint(ReadColor(tint, RuntimeUiColor{})); });
                        ui.set_function("set_element_tint", [](const sol::table &tint)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetElementTint(ReadColor(tint, RuntimeUiColor{})); });
                        ui.set_function("set_background_tint", [](const sol::table &tint)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetBackgroundTint(ReadColor(tint, RuntimeUiColor{})); });
                        ui.set_function("set_element_whiten", [](double amount)
                                        {
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetElementWhiten(static_cast<float>(amount)); });
                        ui.set_function("set_style_background", [](const std::string &styleName, const std::string &path)
                                        {
                                            RuntimeUiQuadVisualStyle style{};
                                            if (!TryParseVisualStyle(styleName, style))
                                            {
                                                PE_WARN("[Lua] runtime_ui.set_style_background: unknown style '%s'", styleName.c_str());
                                                return;
                                            }
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                                runtimeUi->SetStyleBackground(style, path); });
                        ui.set_function("preload_images",
                                        [](const sol::table &pathTable, sol::optional<bool> colorize) -> int
                                        {
                                            RuntimeUiSystem *runtimeUi = RequireRuntimeUi();
                                            if (!runtimeUi)
                                                return 0;
                                            std::vector<std::string> paths;
                                            paths.reserve(pathTable.size());
                                            for (size_t i = 1; i <= pathTable.size(); ++i)
                                            {
                                                if (auto path = pathTable.raw_get<sol::optional<std::string>>(i))
                                                    paths.push_back(*path);
                                            }
                                            return runtimeUi->PreloadImages(paths, colorize.value_or(false));
                                        });
                        ui.set_function("get_surface_size", [](sol::this_state ts) -> sol::table
                                        {
                                            sol::state_view lua(ts);
                                            sol::table result = lua.create_table();
                                            uint32_t width = 0;
                                            uint32_t height = 0;
                                            float uiScale = 1.0f;
                                            float safeX = 0.0f, safeY = 0.0f, safeW = 0.0f, safeH = 0.0f;
                                            bool safeValid = false;
                                            if (RuntimeUiSystem *runtimeUi = RequireRuntimeUi())
                                            {
                                                runtimeUi->GetFrameSurfaceSize(width, height);
                                                uiScale = runtimeUi->GetFrameUiScale();
                                                // Matches SyncSceneWidgets layout rect (Android insets).
                                                safeValid = runtimeUi->GetFrameSafeArea(safeX, safeY, safeW, safeH);
                                            }
                                            result["w"] = width;
                                            result["h"] = height;
                                            result["width"] = width;
                                            result["height"] = height;
                                            // DPI/density font scale (io.FontGlobalScale). 1.0 on ~96dpi
                                            // desktops; up to 4.0 on phones. Resolution-relative UIs divide
                                            // their font_scale by this to cancel the backend's font DPI bump.
                                            result["ui_scale"] = uiScale;
                                            result["safe_x"] = safeX;
                                            result["safe_y"] = safeY;
                                            result["safe_w"] = safeW;
                                            result["safe_h"] = safeH;
                                            result["safe_valid"] = safeValid;
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
