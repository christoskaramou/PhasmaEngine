#include "Properties.h"
#include "CameraWidget.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "imgui/imgui_internal.h" // dock-tab hover flags for the right-click Pin menu
#include "GUI/RuntimeUiAuthoring.h"
#include "GUI/SpriteAuthoring.h"
#include "GUI/SkinnedStrip2DEditor.h"
#include "GUI/UndoRedo.h"
#include "LightWidget.h"
#include "MeshWidget.h"
#include "Particles.h"
#include "PostProcessControls.h"
#include "SceneSettingsControls.h"
#include "Particles/ParticleManager.h"
#include "Render/SceneSky.h"
#include "Scene/ModelAsset.h"
#include "Scene/NodeComponents.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Script/ScriptSystem.h"
#include "ScriptEditor.h"
#include "Systems/AnimationSystem.h"
#include "Systems/RendererSystem.h"
#include "TransformWidget.h"
#ifdef PE_PHYSICS
#include "PhysicsWidget.h"
#include "Physics/PhysicsTypes.h"
#include "Systems/PhysicsSystem.h"
#endif
#ifdef PE_PHYSICS2D
#include "Physics2DWidget.h"
#include "Systems/Physics2DSystem.h"
#endif
#ifdef PE_AUDIO
#include "AudioWidget.h"
#include "Audio/AudioTypes.h"
#include "Systems/AudioSystem.h"
#endif

namespace pe
{
    namespace
    {
        // Path/name text field that commits on deactivate instead of per keystroke. The terrain
        // fields feed the structural reconcile hash — per-keystroke writes rebuilt the world on
        // every typing pause, with half-typed values (ModelAsset warned on the "g" of "grass").
        // One shared buffer suffices: only one item is ever being edited.
        bool InputTextDeferred(const char *label, std::string &value)
        {
            static char s_buf[260];
            static ImGuiID s_editing = 0;
            const ImGuiID id = ImGui::GetID(label);
            if (s_editing != id)
                snprintf(s_buf, sizeof(s_buf), "%s", value.c_str());
            ImGui::InputText(label, s_buf, sizeof(s_buf));
            if (ImGui::IsItemActivated())
                s_editing = id;
            bool committed = false;
            if (s_editing == id)
            {
                if (ImGui::IsItemDeactivatedAfterEdit() && value != s_buf)
                {
                    value = s_buf;
                    committed = true;
                }
                if (ImGui::IsItemDeactivated())
                    s_editing = 0;
            }
            return committed;
        }

        // Built-in primitive meshes for the "Add" menus, sorted by display name. One table drives both
        // the "+ Add Mesh" popup and the "+ Add Component -> Mesh" submenu so the two lists can't drift.
        struct PrimitiveMenuItem
        {
            const char *name;
            ModelAsset *(*make)(); // captureless-lambda thunk: several Create* take defaulted params
            const char *tip;
        };
        constexpr PrimitiveMenuItem kPrimitiveMenu[] = {
            {"Circle", []
             { return Primitives::CreateCircle(); }, "Circle primitive mesh."},
            {"Cone", []
             { return Primitives::CreateCone(); }, "Cone primitive mesh."},
            {"Cube", []
             { return Primitives::CreateCube(); }, "Cube primitive mesh."},
            {"Cylinder", []
             { return Primitives::CreateCylinder(); }, "Cylinder primitive mesh."},
            {"Grid", []
             { return Primitives::CreateGrid(); }, "Subdivided grid primitive mesh."},
            {"Ico Sphere", []
             { return Primitives::CreateIcoSphere(); }, "Ico-sphere primitive mesh."},
            {"Plane", []
             { return Primitives::CreatePlane(); }, "Flat plane primitive mesh."},
            {"Pyramid", []
             { return Primitives::CreatePyramid(); }, "Pyramid primitive mesh."},
            {"Quad", []
             { return Primitives::CreateQuad(); }, "Quad primitive mesh."},
            {"Skinned Strip 2D", []
             { return Primitives::CreateSkinnedStrip2D(); }, "GPU-skinned 2D strip primitive mesh."},
            {"Sphere", []
             { return Primitives::CreateSphere(); }, "Sphere primitive mesh."},
            {"Torus", []
             { return Primitives::CreateTorus(); }, "Torus primitive mesh."},
            {"UV Sphere", []
             { return Primitives::CreateUvSphere(); }, "UV sphere primitive mesh."},
        };

        // Starter templates for the zone's "Create Script" buttons. The Script-section one is a minimal
        // working example; the Physics one (triggers are less obvious) carries extra commented examples.
        constexpr const char *kZoneScriptTemplate =
            "-- Trigger Zone - Script section. Fires when the active camera crosses this zone.\n"
            "-- `self` is the zone node.\n\n"
            "function on_enter(self)\n"
            "    pe_log(\"[zone] enter \" .. self:get_name())\n"
            "end\n\n"
            "function on_exit(self)\n"
            "    pe_log(\"[zone] exit \" .. self:get_name())\n"
            "end\n";

        constexpr const char *kZonePhysicsScriptTemplate =
            "-- Trigger Zone - Physics section (Sensor mode).\n"
            "-- Fires when a PHYSICS BODY overlaps this zone during play.\n"
            "-- `self` = the zone node; `other` = the body that entered/exited (a node handle).\n"
            "-- Set the panel's Filter to limit which bodies fire it (matches the body's node name).\n"
            "-- Tip: click \"Show Functions\" in this editor to see every Lua call available.\n\n"
            "function on_enter(self, other)\n"
            "    pe_log(\"[zone physics] entered: \" .. (other and other:get_name() or \"?\"))\n"
            "    -- Examples (uncomment one) - `other` is the entering body:\n"
            "    -- physics.set_velocity(other, 0, 18, 0)  -- fling it up (reverse its fall)\n"
            "    -- other:set_enabled(false)               -- despawn it\n"
            "    -- physics.apply_impulse(other, 0, 10, 0) -- nudge it upward\n"
            "end\n\n"
            "function on_exit(self, other)\n"
            "    pe_log(\"[zone physics] exited: \" .. (other and other:get_name() or \"?\"))\n"
            "end\n";

        std::vector<std::string> SkyboxExtensions()
        {
            return {".hdr", ".png", ".jpg", ".jpeg", ".tga", ".bmp"};
        }

        const char *SkyboxDisplayPath(const std::string &path)
        {
            return path.empty() ? "<solid color>" : path.c_str();
        }

        std::filesystem::path U8Path(const std::string &utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        void DrawSkyboxPathPreview(const std::string &path, float width = 0.0f)
        {
            const char *displayPath = SkyboxDisplayPath(path);
            width = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
            if (width <= 0.0f)
                return;

            char buffer[512];
            std::snprintf(buffer, sizeof(buffer), "%s", displayPath);
            ImGui::SetNextItemWidth(width);
            if (path.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::InputText("##skybox_path", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
            if (path.empty())
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                std::string tooltip = std::string("Skybox texture path: ") + displayPath;
                ui::TooltipText(tooltip.c_str());
            }
        }

    } // namespace

    void Properties::Update()
    {
        if (!m_open)
            return;

        // Keep size stable: first time always, after that only on appearing
        static bool sizedOnce = false;
        ui::SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, sizedOnce ? ImGuiCond_Appearing : ImGuiCond_Always);
        sizedOnce = true;

        const bool visible = ImGui::Begin("Properties", &m_open);

        // Pinned-tab cue: amber wash + underline over our own dock tab. Paint it before the early-out so it
        // survives when another tab in the node is selected and Begin() hides our body — the tab bar already
        // stored DockTabItemRect for every docked window this frame. Dim it when we're not the active tab.
        if (m_pin.active)
        {
            ImGuiWindow *win = ImGui::GetCurrentWindow();
            if (win && win->DockIsActive && win->DC.DockTabItemRect.GetWidth() > 0.0f)
            {
                const ImRect r = win->DC.DockTabItemRect;
                const float dim = visible ? 1.0f : 0.4f;
                ImDrawList *dl = ImGui::GetForegroundDrawList();
                dl->AddRectFilled(r.Min, r.Max, IM_COL32(255, 170, 40, (int)(55 * dim)));
                dl->AddLine(ImVec2(r.Min.x, r.Max.y - 1.0f), ImVec2(r.Max.x, r.Max.y - 1.0f),
                            IM_COL32(255, 170, 40, (int)(255 * dim)), 2.0f);
            }
        }

        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto &sel = SelectionManager::Instance();
        Scene &scene = *GetActiveScene();

        // Drop a stale pin (the pinned node was deleted) before anything reads it.
        if (m_pin.active && (m_pin.type == SelectionType::Node || m_pin.type == SelectionType::Mesh) &&
            !scene.IsNodeAlive(m_pin.node))
            m_pin.active = false;

        // Pin (Inspector lock) via right-click on the Properties dock tab. Pinned = the panel keeps showing
        // the captured object while the global selection changes; right-click again to unpin.
        {
            ImGuiWindow *win = ImGui::GetCurrentWindow();
            if (win && win->DockIsActive)
            {
                const ImGuiItemStatusFlags tf = win->DC.DockTabItemStatusFlags;
                if ((tf & ImGuiItemStatusFlags_HoveredRect) && (tf & ImGuiItemStatusFlags_HoveredWindow) &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##props_pin_ctx");
            }
            if (ImGui::BeginPopup("##props_pin_ctx"))
            {
                if (ImGui::MenuItem(m_pin.active ? "Unpin Properties" : "Pin Properties"))
                {
                    if (m_pin.active)
                        m_pin.active = false;
                    else if (sel.HasSelection())
                    {
                        m_pin.active = true;
                        m_pin.type = sel.GetSelectionType();
                        m_pin.node = sel.GetSelectedNode();
                        m_pin.lightType = sel.GetSelectedLightType();
                        m_pin.lightIndex = sel.GetSelectedLightIndex();
                        m_pin.emitterIndex = sel.GetSelectedEmitterIndex();
                        m_pin.cameraIndex = sel.GetSelectedCameraIndex();
                    }
                }
                ImGui::EndPopup();
            }
        }

        // Effective selection the panel shows: the pinned snapshot when locked, otherwise the live selection.
        const bool usePin = m_pin.active;
        if (!usePin && !sel.HasSelection())
        {
            ImGui::TextDisabled("No object selected");
            ImGui::End();
            return;
        }
        const SelectionType viewType = usePin ? m_pin.type : sel.GetSelectionType();
        NodeId *viewNode = usePin ? m_pin.node : sel.GetSelectedNode();
        const LightType viewLightType = usePin ? m_pin.lightType : sel.GetSelectedLightType();
        const int viewLightIndex = usePin ? m_pin.lightIndex : sel.GetSelectedLightIndex();
        const int viewEmitterIndex = usePin ? m_pin.emitterIndex : sel.GetSelectedEmitterIndex();
        const int viewCameraIndex = usePin ? m_pin.cameraIndex : sel.GetSelectedCameraIndex();

        if ((viewType == SelectionType::Node || viewType == SelectionType::Mesh) && !scene.IsNodeAlive(viewNode))
        {
            if (!usePin)
                sel.ClearSelection();
            ImGui::TextDisabled("No object selected");
            ImGui::End();
            return;
        }

        auto drawTransform = [&]()
        {
            if (auto *w = m_gui->GetWidget<TransformWidget>())
                w->DrawEmbed(viewNode);
        };

        auto attachPrimitive = [&](NodeId *node, ModelAsset *model)
        {
            EventSystem::PushEvent(EventType::PrimitiveAttachedToNode, Scene::PrimitiveAttachRequest{node, model});
        };

        auto drawMeshComponent = [&](NodeId *node)
        {
            auto *w = m_gui->GetWidget<MeshWidget>();
            if (!w)
                return;

            const auto &refs = scene.GetNodeCache(node).meshRefs->meshRefs;
            int removeIdx = -1;
            for (int slot = 0; slot < static_cast<int>(refs.size()); slot++)
            {
                int meshIndex = refs[slot];
                if (meshIndex < 0)
                    continue;

                Mesh &mesh = scene.GetMesh(meshIndex);

                if (refs.size() > 1)
                {
                    ImGui::PushID(slot);
                    bool open = ImGui::CollapsingHeader(
                        ("Mesh " + std::to_string(slot)).c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen);
                    ui::ItemTooltip("Show controls for this mesh slot on the selected node.");
                    if (open)
                    {
                        w->DrawEmbed(&mesh, node);
                        if (ImGui::SmallButton("Remove"))
                            removeIdx = meshIndex;
                        ui::ItemTooltip("Remove this mesh slot from the selected node.");
                    }
                    ImGui::PopID();
                }
                else
                {
                    w->DrawEmbed(&mesh, node);
                }
            }

            ImGui::Dummy(ImVec2(0.f, 2.f));

            if (ImGui::SmallButton("+ Add Mesh"))
                ImGui::OpenPopup("AddMeshPopup");
            ui::ItemTooltip("Attach an additional primitive mesh to this node.");

            if (ImGui::BeginPopup("AddMeshPopup"))
            {
                for (const auto &p : kPrimitiveMenu)
                {
                    if (ImGui::MenuItem(p.name))
                        attachPrimitive(node, p.make());
                    ui::ItemTooltip(p.tip);
                }
                ImGui::EndPopup();
            }

            if (removeIdx >= 0)
                scene.RemoveMeshRef(node, removeIdx);
        };

        auto drawScriptComponent = [&](NodeId *node)
        {
            ScriptSystem *ss = GetGlobalSystem<ScriptSystem>();
            NodeScriptInstance *inst = ss ? ss->FindNodeInstance(node) : nullptr;

            if (inst && !inst->lastError.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
                ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", inst->lastError.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            const std::string &scriptPath = scene.GetNodeScriptPath(node);
            ImGui::TextWrapped("%s", scriptPath.c_str());
            if (ImGui::SmallButton("Edit Script"))
            {
                if (auto *se = m_gui->GetWidget<ScriptEditor>())
                    se->OpenScript(node, scriptPath);
            }
            ui::ItemTooltip("Open this node's Lua script in the script editor.");

            ImGui::Spacing();
            int runMode = static_cast<int>(scene.GetNodeScriptRunMode(node));
            const char *runModeNames[] = {"Player", "Editor", "Both"};
            ImGui::SetNextItemWidth(140.f);
            if (ImGui::Combo("Run In", &runMode, runModeNames, 3))
                scene.SetNodeScriptRunMode(node, static_cast<ScriptRunMode>(runMode));
            ui::ItemTooltip("When this script's init/update/destroy run: Player (play mode + the "
                            "player), Editor (while editing, not in play), or Both.");

            // Show exposed variables from the per-node script instance
            if (!ss)
                return;

            if (!inst || inst->exposedVars.empty())
                return;

            ImGui::Dummy(ImVec2(0.f, 4.f));
            ImGui::SeparatorText("Exposed Variables");

            if (inst->exposedRef == LUA_NOREF)
                return;
            lua_State *L = ss->GetLua().lua_state();
            lua_rawgeti(L, LUA_REGISTRYINDEX, inst->exposedRef);
            if (!lua_istable(L, -1))
            {
                PE_WARN("[Properties] Invalid exposed table ref for node script '%s'", scriptPath.c_str());
                lua_pop(L, 1);
                return;
            }
            sol::table exposed(L, -1);
            lua_pop(L, 1);

            for (auto &var : inst->exposedVars)
            {
                ImGui::PushID(var.name.c_str());
                switch (var.type)
                {
                case ExposedVar::Type::Number:
                {
                    sol::optional<double> opt = exposed[var.name];
                    if (!opt)
                        break;
                    float val = static_cast<float>(*opt);
                    if (ImGui::DragFloat(var.name.c_str(), &val, 0.1f))
                        exposed[var.name] = static_cast<double>(val);
                    ui::ItemTooltip("Edit this numeric value exposed by the node script.");
                    break;
                }
                case ExposedVar::Type::Bool:
                {
                    sol::optional<bool> opt = exposed[var.name];
                    if (!opt)
                        break;
                    bool val = *opt;
                    if (ImGui::Checkbox(var.name.c_str(), &val))
                        exposed[var.name] = val;
                    ui::ItemTooltip("Toggle this boolean value exposed by the node script.");
                    break;
                }
                case ExposedVar::Type::String:
                {
                    sol::optional<std::string> opt = exposed[var.name];
                    if (!opt)
                        break;
                    std::string val = *opt;
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "%s", val.c_str());
                    if (ImGui::InputText(var.name.c_str(), buf, sizeof(buf)))
                        exposed[var.name] = std::string(buf);
                    ui::ItemTooltip("Edit this string value exposed by the node script.");
                    break;
                }
                }
                ImGui::PopID();
            }
        };

        auto drawCamera = [&]()
        {
            auto *w = m_gui->GetWidget<CameraWidget>();
            if (!w)
                return;

            const int index = viewCameraIndex;
            auto &cameras = scene.GetCameras();
            if (index < 0 || index >= (int)cameras.size())
                return;

            w->DrawEmbed(cameras[index]);
        };

        auto drawLight = [&]()
        {
            auto *w = m_gui->GetWidget<LightWidget>();
            if (!w)
                return;

            w->DrawEmbed(&scene, viewLightType, viewLightIndex);
        };

        auto drawEmitter = [&]()
        {
            auto *w = m_gui->GetWidget<Particles>();
            if (!w)
                return;

            RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
                return;

            ParticleManager *pm = renderer->GetScene().GetParticleManager();
            if (!pm)
                return;

            w->DrawEmbed(pm, viewEmitterIndex);
        };

        auto drawRuntimeUiComponent = [&](NodeId *node)
        {
            NodeRuntimeUiTag *uiComponent = scene.GetRuntimeUiComponent(node);
            if (!uiComponent)
                return;

            auto markRuntimeUiChanged = [&]()
            {
                uiComponent->authored = true;
                scene.MarkNodeDirty(node);
                scene.MarkDirty();
                if (m_gui)
                    m_gui->NotifyChange();
            };

            if (!uiComponent->authored)
            {
                ImGui::TextDisabled("Runtime UI tag");
                if (ImGui::SmallButton("Create Authored UI"))
                {
                    RuntimeUiAuthoring::ConfigureDefaults(*uiComponent, NodeRuntimeUiWidgetType::Panel);
                    markRuntimeUiChanged();
                }
                ui::ItemTooltip("Add editable Runtime UI widget data to this node.");
                return;
            }

            const auto &templates = RuntimeUiAuthoring::Templates();
            int currentType = 0;
            for (int i = 0; i < static_cast<int>(templates.size()); ++i)
                if (templates[i].type == uiComponent->widgetType)
                    currentType = i;

            if (ImGui::BeginCombo("Type", templates[currentType].name))
            {
                for (int i = 0; i < static_cast<int>(templates.size()); ++i)
                {
                    const bool selected = i == currentType;
                    if (ImGui::Selectable(templates[i].name, selected))
                    {
                        RuntimeUiAuthoring::ConfigureDefaults(*uiComponent, templates[i].type);
                        markRuntimeUiChanged();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ui::ItemTooltip("Choose the authored Runtime UI element type.");

            auto editString = [&](const char *label, std::string &value)
            {
                char buffer[512];
                snprintf(buffer, sizeof(buffer), "%s", value.c_str());
                if (ImGui::InputText(label, buffer, sizeof(buffer)))
                {
                    value = buffer;
                    markRuntimeUiChanged();
                }
            };

            editString("Label", uiComponent->label);
            editString("Title", uiComponent->title);
            editString("Subtitle", uiComponent->subtitle);
            editString("Body", uiComponent->body);
            editString("Footer", uiComponent->footer);
            editString("Image", uiComponent->imagePath);

            struct RuntimeUiActionOption
            {
                const char *id;
                const char *label;
            };
            static constexpr RuntimeUiActionOption kRuntimeUiActions[] = {
                {"click", "Click"},
                {"hover_enter", "Hover Enter"},
                {"press", "Press"},
                {"release", "Release"},
                {"drag_start", "Drag Start"},
                {"dragging", "Dragging"},
                {"drag_release", "Drag Release"},
            };
            static constexpr int kRuntimeUiActionCount = static_cast<int>(sizeof(kRuntimeUiActions) / sizeof(kRuntimeUiActions[0]));
            auto findActionIndex = [&]()
            {
                for (int i = 0; i < kRuntimeUiActionCount; ++i)
                    if (uiComponent->actionName == kRuntimeUiActions[i].id)
                        return i;
                return 0;
            };

            int currentAction = findActionIndex();
            if (ImGui::BeginCombo("Action", kRuntimeUiActions[currentAction].label))
            {
                for (int i = 0; i < kRuntimeUiActionCount; ++i)
                {
                    const bool selected = i == currentAction;
                    if (ImGui::Selectable(kRuntimeUiActions[i].label, selected))
                    {
                        uiComponent->actionName = kRuntimeUiActions[i].id;
                        markRuntimeUiChanged();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ui::ItemTooltip("Choose when the Action Function runs.");
            editString("Action Function", uiComponent->actionFunction);
            ui::ItemTooltip("Lua function to call on this node's script when the selected action fires. Leave empty for no action.");
            if (!uiComponent->actionFunction.empty() && !(scene.GetComponentFlags(node) & Component_Script))
                ImGui::TextDisabled("Attach a Script component to run this action.");

            auto editColor = [&](const char *label, vec4 &color)
            {
                float value[4] = {color.r, color.g, color.b, color.a};
                if (ImGui::ColorEdit4(label, value))
                {
                    color = vec4(value[0], value[1], value[2], value[3]);
                    markRuntimeUiChanged();
                }
            };

            editColor("Fill", uiComponent->fillColor);
            editColor("Border", uiComponent->borderColor);
            editColor("Accent", uiComponent->accentColor);
            editColor("Text", uiComponent->textColor);
            editColor("Image Tint", uiComponent->imageTint);
            if (ImGui::Checkbox("Use Background Tint", &uiComponent->useBackgroundTint))
                markRuntimeUiChanged();
            ui::ItemTooltip("Route this widget through the global background tint instead of the element tint.");

            float anchorVals[2] = {uiComponent->anchor.x, uiComponent->anchor.y};
            if (ImGui::DragFloat2("Anchor", anchorVals, 0.01f, 0.0f, 1.0f, "%.2f"))
            {
                uiComponent->anchor = vec2(anchorVals[0], anchorVals[1]);
                markRuntimeUiChanged();
            }
            ui::ItemTooltip("Screen anchor (0..1): 0,0 = top-left, 0.5,0.5 = center, 1,1 = bottom-right. The element stays at this screen point across resolutions.");
            float pivotVals[2] = {uiComponent->pivot.x, uiComponent->pivot.y};
            if (ImGui::DragFloat2("Pivot", pivotVals, 0.01f, 0.0f, 1.0f, "%.2f"))
            {
                uiComponent->pivot = vec2(pivotVals[0], pivotVals[1]);
                markRuntimeUiChanged();
            }
            ui::ItemTooltip("Element pivot (0..1): the transform position places this point of the element. 0.5,0.5 = center.");

            // Uncapped: floor at a small positive value, no upper bound (FLT_MAX) so
            // titles/HUD text can scale arbitrarily large. Ctrl+click the slider to type.
            if (ImGui::DragFloat("Font Scale", &uiComponent->fontScale, 0.05f, 0.01f, FLT_MAX, "%.2f"))
                markRuntimeUiChanged();

            const char *hAlignItems[] = {"Default", "Left", "Center", "Right"};
            int hAlign = uiComponent->textAlignH < 4 ? static_cast<int>(uiComponent->textAlignH) : 0;
            if (ImGui::Combo("Text Align X", &hAlign, hAlignItems, IM_ARRAYSIZE(hAlignItems)))
            {
                uiComponent->textAlignH = static_cast<uint8_t>(hAlign);
                markRuntimeUiChanged();
            }
            ui::ItemTooltip("Horizontal text alignment. Default = Left for text/panels, Center for buttons.");
            const char *vAlignItems[] = {"Default", "Top", "Middle", "Bottom"};
            int vAlign = uiComponent->textAlignV < 4 ? static_cast<int>(uiComponent->textAlignV) : 0;
            if (ImGui::Combo("Text Align Y", &vAlign, vAlignItems, IM_ARRAYSIZE(vAlignItems)))
            {
                uiComponent->textAlignV = static_cast<uint8_t>(vAlign);
                markRuntimeUiChanged();
            }
            ui::ItemTooltip("Vertical text alignment. Default = Top for text/panels, Middle for buttons.");
            float textOffsetVals[2] = {uiComponent->textOffset.x, uiComponent->textOffset.y};
            if (ImGui::DragFloat2("Text Offset", textOffsetVals, 0.5f, -4000.0f, 4000.0f, "%.0f"))
            {
                uiComponent->textOffset = vec2(textOffsetVals[0], textOffsetVals[1]);
                markRuntimeUiChanged();
            }
            ui::ItemTooltip("Pixel nudge applied to the text after alignment (x, y).");

            if (ImGui::Checkbox("Visible", &uiComponent->visible))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("Draggable", &uiComponent->draggable))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("No Input", &uiComponent->noInput))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("Bring To Front", &uiComponent->bringToFront))
                markRuntimeUiChanged();
        };

        auto drawSpriteComponent = [&](NodeId *node)
        {
            NodeSpriteComponent *sprite = scene.GetSpriteComponent(node);
            if (!sprite)
                return;

            auto drawTextRow = [](const char *label, const std::string &value)
            {
                ImGui::TextUnformatted(label);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", value.empty() ? "<none>" : value.c_str());
            };

            drawTextRow("Image", sprite->imagePath);
            drawTextRow("Metadata", sprite->metadataPath);
            drawTextRow("Frame", sprite->frameName.empty() ? std::to_string(sprite->frameIndex) : sprite->frameName);
            ImGui::TextDisabled("Image %dx%d  Frame %dx%d", sprite->imageWidth, sprite->imageHeight, sprite->frameWidth, sprite->frameHeight);
            ImGui::TextDisabled("UV %.3f %.3f %.3f %.3f", sprite->uvRect.x, sprite->uvRect.y, sprite->uvRect.z, sprite->uvRect.w);

            auto applySpriteAsset = [node, this](const std::string &path) -> bool
            {
                auto *renderer = GetGlobalSystem<RendererSystem>();
                if (!renderer)
                    return true;

                Scene &targetScene = renderer->GetScene();
                if (!targetScene.IsNodeAlive(node))
                    return true;

                SpriteAuthoring::Options options;
                options.assetPath = U8Path(path);
                SpriteAuthoring::Result result = SpriteAuthoring::ApplyToNode(targetScene, node, options);
                if (!result.error.empty())
                    PE_WARN("[Sprite] %s", result.error.c_str());
                if (m_gui)
                    m_gui->NotifyChange();
                return true;
            };

            if (ImGui::SmallButton("Select Image"))
            {
                if (auto *fs = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr)
                    fs->OpenSelection(applySpriteAsset, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".dds", ".ktx", ".ktx2"}, Path::Assets);
            }
            ui::ItemTooltip("Choose an image asset for this sprite.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Select Metadata"))
            {
                if (auto *fs = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr)
                    fs->OpenSelection(applySpriteAsset, {".json"}, Path::Assets);
            }
            ui::ItemTooltip("Choose a .sprite.json metadata asset for this sprite.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reload Metadata"))
            {
                std::string err;
                if (!scene.LoadSpriteMetadata(node, &err))
                    PE_WARN("[Sprite] %s", err.c_str());
            }
            ui::ItemTooltip("Reload frames and clips from the metadata file.");

            int frameIndex = sprite->frameIndex;
            ImGui::SetNextItemWidth(96.0f);
            if (ImGui::DragInt("Frame", &frameIndex, 1.0f, 0, std::max(0, static_cast<int>(sprite->frames.size()) - 1)))
            {
                std::string err;
                if (!scene.SetSpriteFrame(node, frameIndex, sprite->meshSlot, &err))
                    PE_WARN("[Sprite] %s", err.c_str());
                if (m_gui)
                    m_gui->NotifyChange();
            }

            vec4 tint = sprite->tint;
            if (ImGui::ColorEdit4("Tint", &tint.x))
            {
                SpriteAuthoring::Options options;
                options.hasTint = true;
                options.tint = tint;
                SpriteAuthoring::Result result = SpriteAuthoring::ApplyToNode(scene, node, options, sprite->meshSlot);
                if (!result.error.empty())
                    PE_WARN("[Sprite] %s", result.error.c_str());
                if (m_gui)
                    m_gui->NotifyChange();
            }

            if (!sprite->clips.empty())
            {
                int clipIndex = std::clamp(sprite->activeClipIndex >= 0 ? sprite->activeClipIndex : 0, 0, static_cast<int>(sprite->clips.size()) - 1);
                std::vector<const char *> clipNames;
                clipNames.reserve(sprite->clips.size());
                for (const NodeSpriteClip &clip : sprite->clips)
                    clipNames.push_back(clip.name.c_str());

                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("Clip", &clipIndex, clipNames.data(), static_cast<int>(clipNames.size())))
                {
                    sprite->activeClipIndex = clipIndex;
                    sprite->activeClipName = sprite->clips[clipIndex].name;
                    sprite->loop = sprite->clips[clipIndex].loop;
                    if (m_gui)
                        m_gui->NotifyChange();
                }

                ImGui::SetNextItemWidth(96.0f);
                if (ImGui::DragFloat("Speed", &sprite->playbackSpeed, 0.05f, 0.0f, 16.0f, "%.2f"))
                {
                    sprite->playbackSpeed = std::clamp(sprite->playbackSpeed, 0.0f, 16.0f);
                    if (m_gui)
                        m_gui->NotifyChange();
                }
                if (ImGui::Checkbox("Loop", &sprite->loop))
                {
                    if (m_gui)
                        m_gui->NotifyChange();
                }
                bool interpolate = sprite->interpolate;
                if (ImGui::Checkbox("Interpolate Frames", &interpolate))
                {
                    scene.SetSpriteInterpolation(node, interpolate);
                    if (m_gui)
                        m_gui->NotifyChange();
                }
                ui::ItemTooltip("Blend the current atlas frame into the next frame.");

                if (ImGui::SmallButton(sprite->playing ? "Pause" : "Play"))
                {
                    if (sprite->playing)
                        scene.SetSpritePlaying(node, false);
                    else
                    {
                        std::string err;
                        if (!scene.PlaySpriteClip(node, sprite->clips[clipIndex].name, false, sprite->meshSlot, &err))
                            PE_WARN("[Sprite] %s", err.c_str());
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Restart"))
                {
                    std::string err;
                    if (!scene.PlaySpriteClip(node, sprite->clips[clipIndex].name, true, sprite->meshSlot, &err))
                        PE_WARN("[Sprite] %s", err.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Stop"))
                    scene.StopSprite(node);
            }
            else
            {
                ImGui::TextDisabled("No sprite clips loaded.");
            }
        };

        auto drawAnimationRuntime = [&](NodeId *node) -> bool
        {
            AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
            if (!anim)
                return false;

            const auto &clips = scene.GetAnimationClipsForNode(node);
            const AnimationNodeState *state = anim->GetAnimationState(node);
            const bool hasAnimationSurface = state || (scene.NodeHasSkinnedMesh(node) && !clips.empty());
            if (!hasAnimationSurface)
                return false;

            if (clips.empty())
            {
                ImGui::TextDisabled("No animation clips");
                return true;
            }

            int clipIndex = state && state->clipIndex >= 0 && state->clipIndex < static_cast<int>(clips.size()) ? state->clipIndex : 0;
            std::vector<const char *> clipNames;
            clipNames.reserve(clips.size());
            for (const AnimationClip &clip : clips)
                clipNames.push_back(clip.name.empty() ? "<unnamed>" : clip.name.c_str());

            bool loop = state ? state->loop : true;
            if (ImGui::Combo("Clip", &clipIndex, clipNames.data(), static_cast<int>(clipNames.size())))
            {
                anim->PlayAnimation(scene, node, clipIndex, loop);
                state = anim->GetAnimationState(node);
            }
            ui::ItemTooltip("Choose the animation clip to preview on this node.");

            const bool playing = state && state->playing;
            if (ImGui::Button(playing ? "Pause" : "Play"))
            {
                if (playing)
                {
                    anim->SetPaused(node, true);
                }
                else if (state && state->clipIndex == clipIndex)
                {
                    anim->SetPaused(node, false);
                }
                else
                {
                    anim->PlayAnimation(scene, node, clipIndex, loop);
                }
                state = anim->GetAnimationState(node);
            }
            ui::ItemTooltip(playing ? "Pause the current animation preview." : "Start or resume the selected animation preview.");

            ImGui::SameLine();
            if (!state)
                ImGui::BeginDisabled();
            if (ImGui::Button("Stop"))
            {
                anim->StopAnimation(node);
                state = anim->GetAnimationState(node);
            }
            ui::ItemTooltip("Stop animation playback and clear the preview state.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);
            if (!state)
                ImGui::EndDisabled();

            loop = state ? state->loop : loop;
            if (ImGui::Checkbox("Loop", &loop))
            {
                if (state)
                    anim->SetLoop(node, loop);
            }
            ui::ItemTooltip("Loop the selected animation clip during playback.");

            float speed = state ? state->speed : 1.0f;
            if (ImGui::DragFloat("Speed", &speed, 0.01f, -10.0f, 10.0f, "%.3f"))
            {
                if (state)
                    anim->SetSpeed(node, speed);
            }
            ui::ItemTooltip("Playback speed multiplier; negative values play backward.");

            if (state && clipIndex >= 0 && clipIndex < static_cast<int>(clips.size()))
            {
                const AnimationClip &clip = clips[clipIndex];
                float time = state->time;
                if (ImGui::SliderFloat("Time", &time, 0.0f, std::max(clip.duration, 0.001f), "%.2f ticks"))
                {
                    anim->SetPlaybackTime(scene, node, time);
                    state = anim->GetAnimationState(node);
                }
                ui::ItemTooltip("Scrub the active animation to a specific tick.");
                const float tps = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 1.0f;
                ImGui::Text("Time: %.3fs / %.3fs", time / tps, clip.duration / tps);
            }
            else
            {
                ImGui::TextDisabled("No active playback state");
            }

            return true;
        };

        auto drawSkinnedStrip2D = [&](NodeId *node) -> bool
        {
            if (!scene.NodeUsesSkinnedStrip2D(node))
                return false;

            AnimationSystem *anim = GetGlobalSystem<AnimationSystem>();
            skinned_strip_2d_editor::State &stripState = skinned_strip_2d_editor::GetState(scene, node);

            if (ModelAsset *model = scene.GetModelForNode(node))
            {
                const vec4 &params = model->GetPrimitiveParams();
                if (model->GetPrimitiveParamCount() >= 4)
                {
                    ImGui::Text("Size: %.2f x %.2f", params.x, params.y);
                    ImGui::Text("Segments: %d", static_cast<int>(params.z));
                }
            }
            ImGui::Text("Joints: %d", stripState.jointCount);

            if (!anim || stripState.jointCount <= 0)
                ImGui::BeginDisabled();

            auto notifyPoseEdit = [&]()
            {
                if (m_gui)
                    m_gui->NotifyChange();
            };

            auto applyRotations = [&]() -> bool
            {
                if (!anim || stripState.jointCount <= 0)
                    return false;
                const bool applied = skinned_strip_2d_editor::ApplyRotations(anim, scene, node, stripState);
                if (applied)
                    notifyPoseEdit();
                return applied;
            };

            if (ImGui::Button("Reset Pose"))
            {
                stripState.rotationsRadians.assign(static_cast<size_t>(stripState.jointCount), 0.0f);
                stripState.stretchScale = 1.0f;
                applyRotations();
            }
            ui::ItemTooltip("Reset the generated strip to its bind pose.");
            ImGui::SameLine();
            if (ImGui::Button("Bind Target"))
            {
                stripState.ikTargetLocal = skinned_strip_2d_editor::GetBindEndLocal(scene, node);
                skinned_strip_2d_editor::PersistState(scene, node, stripState);
                notifyPoseEdit();
            }
            ui::ItemTooltip("Move the IK target back to the strip's bind-pose tip.");

            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragInt("IK Iterations", &stripState.ikIterations, 0.25f, 1, 64))
            {
                stripState.ikIterations = std::clamp(stripState.ikIterations, 1, 64);
                skinned_strip_2d_editor::PersistState(scene, node, stripState);
                notifyPoseEdit();
            }
            ui::ItemTooltip("Solver iterations used when applying the IK target.");

            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragFloat("Bend Limit",
                                 &stripState.maxBendDegrees,
                                 0.25f,
                                 skinned_strip_2d_editor::kMinBendLimitDegrees,
                                 skinned_strip_2d_editor::kMaxBendLimitDegrees,
                                 "%.1f deg"))
            {
                stripState.maxBendDegrees = std::clamp(stripState.maxBendDegrees,
                                                       skinned_strip_2d_editor::kMinBendLimitDegrees,
                                                       skinned_strip_2d_editor::kMaxBendLimitDegrees);
                if (skinned_strip_2d_editor::SolveIk(anim, scene, node, stripState))
                    notifyPoseEdit();
            }
            ui::ItemTooltip("Maximum local bend used while solving IK.");

            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragFloat("Stretch Limit",
                                 &stripState.maxStretchScale,
                                 0.01f,
                                 skinned_strip_2d_editor::kMinStretchScale,
                                 skinned_strip_2d_editor::kMaxStretchLimit,
                                 "%.2fx"))
            {
                stripState.maxStretchScale = std::clamp(stripState.maxStretchScale,
                                                        skinned_strip_2d_editor::kMinStretchScale,
                                                        skinned_strip_2d_editor::kMaxStretchLimit);
                if (stripState.stretchScale > stripState.maxStretchScale)
                    stripState.stretchScale = stripState.maxStretchScale;
                if (skinned_strip_2d_editor::SolveIk(anim, scene, node, stripState))
                    notifyPoseEdit();
            }
            ui::ItemTooltip("Maximum elongation allowed while solving IK.");

            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragFloat("Pose Stretch",
                                 &stripState.stretchScale,
                                 0.01f,
                                 skinned_strip_2d_editor::kMinStretchScale,
                                 stripState.maxStretchScale,
                                 "%.2fx"))
            {
                stripState.stretchScale = std::clamp(stripState.stretchScale,
                                                     skinned_strip_2d_editor::kMinStretchScale,
                                                     stripState.maxStretchScale);
                applyRotations();
            }
            ui::ItemTooltip("Current strip elongation for direct joint posing.");

            if (ImGui::DragFloat2("IK Target", &stripState.ikTargetLocal.x, 0.02f, -100.0f, 100.0f, "%.2f"))
            {
                if (skinned_strip_2d_editor::SolveIk(anim, scene, node, stripState))
                    notifyPoseEdit();
            }
            ui::ItemTooltip("Node-local XY target for the generated strip chain.");

            if (ImGui::Button("Solve IK"))
            {
                if (skinned_strip_2d_editor::SolveIk(anim, scene, node, stripState))
                    notifyPoseEdit();
            }
            ui::ItemTooltip("Apply the current IK target to the strip pose.");

            if (ImGui::TreeNodeEx("Joint Influences", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (stripState.jointInfluences.size() != static_cast<size_t>(stripState.jointCount))
                    stripState.jointInfluences.resize(static_cast<size_t>(stripState.jointCount), 1.0f);

                if (ImGui::Button("Reset Influences"))
                {
                    stripState.jointInfluences.assign(static_cast<size_t>(stripState.jointCount), 1.0f);
                    if (skinned_strip_2d_editor::SolveIk(anim, scene, node, stripState))
                        notifyPoseEdit();
                }
                ui::ItemTooltip("Restore all IK bend weights to normal.");

                const int influenceCount = std::max(stripState.jointCount - 1, 0);
                for (int i = 0; i < influenceCount; i++)
                {
                    ImGui::PushID(i);
                    ImGui::Text("J%02d", i);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    float &influence = stripState.jointInfluences[static_cast<size_t>(i)];
                    if (ImGui::SliderFloat("##joint_influence",
                                           &influence,
                                           skinned_strip_2d_editor::kMinJointInfluence,
                                           skinned_strip_2d_editor::kMaxJointInfluence,
                                           "%.2f"))
                    {
                        influence = std::clamp(influence,
                                               skinned_strip_2d_editor::kMinJointInfluence,
                                               skinned_strip_2d_editor::kMaxJointInfluence);
                        if (skinned_strip_2d_editor::SolveIk(anim, scene, node, stripState))
                            notifyPoseEdit();
                    }
                    ImGui::PopID();
                }

                ImGui::TreePop();
            }
            ui::ItemTooltip("Set per-joint bend weights used by the IK solver.");

            if (ImGui::TreeNodeEx("Width Scales", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (stripState.widthScales.size() != static_cast<size_t>(stripState.jointCount))
                    stripState.widthScales.resize(static_cast<size_t>(stripState.jointCount), 1.0f);

                if (ImGui::Button("Reset Widths"))
                {
                    stripState.widthScales.assign(static_cast<size_t>(stripState.jointCount), 1.0f);
                    applyRotations();
                }
                ui::ItemTooltip("Restore all strip width scales to normal.");

                for (int i = 0; i < stripState.jointCount; i++)
                {
                    ImGui::PushID(i);
                    ImGui::Text("J%02d", i);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    float &widthScale = stripState.widthScales[static_cast<size_t>(i)];
                    if (ImGui::SliderFloat("##width_scale",
                                           &widthScale,
                                           skinned_strip_2d_editor::kMinWidthScale,
                                           skinned_strip_2d_editor::kMaxWidthScale,
                                           "%.2fx"))
                    {
                        widthScale = std::clamp(widthScale,
                                                skinned_strip_2d_editor::kMinWidthScale,
                                                skinned_strip_2d_editor::kMaxWidthScale);
                        applyRotations();
                    }
                    ImGui::PopID();
                }

                ImGui::TreePop();
            }
            ui::ItemTooltip("Set per-joint thickness scales for tapering or bulging the strip.");

            if (ImGui::TreeNodeEx("Joint Rotations", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static constexpr float kRadToDeg = 57.29577951308232f;
                static constexpr float kDegToRad = 0.017453292519943295f;

                if (stripState.rotationsRadians.size() != static_cast<size_t>(stripState.jointCount))
                    stripState.rotationsRadians.resize(static_cast<size_t>(stripState.jointCount), 0.0f);

                for (int i = 0; i < stripState.jointCount; i++)
                {
                    ImGui::PushID(i);
                    ImGui::Text("J%02d", i);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    float degrees = stripState.rotationsRadians[static_cast<size_t>(i)] * kRadToDeg;
                    if (ImGui::SliderFloat("##rotation_z", &degrees, -180.0f, 180.0f, "%.1f deg"))
                    {
                        stripState.rotationsRadians[static_cast<size_t>(i)] = degrees * kDegToRad;
                        applyRotations();
                    }
                    ImGui::PopID();
                }

                ImGui::TreePop();
            }
            ui::ItemTooltip("Edit per-joint local Z rotations for procedural strip posing.");

            if (!anim || stripState.jointCount <= 0)
                ImGui::EndDisabled();

            return true;
        };

        // Add/Remove bar. Drawn right under Node Info + Transform so both stay reachable
        // without scrolling past every component section.
        auto drawComponentBar = [&](NodeId *node)
        {
            const uint32_t flags = scene.GetComponentFlags(node);
            uint32_t removable = flags & (Component_Mesh | Component_Script | Component_RuntimeUi | Component_Sprite);
#ifdef PE_AUDIO
            removable |= flags & Component_Audio;
#endif
#ifdef PE_PHYSICS
            removable |= flags & Component_Physics;
#endif
#ifdef PE_PHYSICS2D
            removable |= flags & Component_Physics2D;
#endif

            ImGui::Dummy(ImVec2(0.f, 4.f));
            ImGui::Separator();

            const float btnSize = ImGui::GetFrameHeight();
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 (ImGui::GetContentRegionAvail().x - btnSize * 2.f - gap) * 0.5f);

            if (ui::CenteredIconButton("##AddComponent", ICON_FA_PLUS, ImVec2(btnSize, btnSize)))
                ImGui::OpenPopup("AddComponentPopup");
            ui::ItemTooltip("Open the menu of components that can be added to this node.");

            ImGui::SameLine(0.f, gap);
            ImGui::BeginDisabled(removable == 0);
            if (ui::CenteredIconButton("##RemoveComponent", ICON_FA_MINUS, ImVec2(btnSize, btnSize)))
                ImGui::OpenPopup("RemoveComponentPopup");
            ImGui::EndDisabled();
            ui::ItemTooltip("Remove one of the components currently on this node.",
                            ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                // Items sorted by display name. Conditional/#ifdef blocks keep their own gating.
#ifdef PE_AUDIO
                if (!(flags & Component_Audio))
                {
                    if (ImGui::MenuItem("Audio Source"))
                    {
                        if (auto *as = GetGlobalSystem<AudioSystem>())
                        {
                            AudioSourceDesc desc;
                            as->AddSource(scene, node, desc);
                        }
                    }
                    ui::ItemTooltip("Add an audio source component.");
                }
#endif

                if (!(flags & Component_Script))
                {
                    const bool scriptMenuOpen = ImGui::BeginMenu("Lua Script");
                    ui::ItemTooltip("Attach a Lua script component to this node.");
                    if (scriptMenuOpen)
                    {
                        if (ImGui::MenuItem("Browse Existing..."))
                        {
                            if (auto *fs = m_gui->GetWidget<FileSelector>())
                            {
                                fs->OpenSelection([node](const std::string &path) -> bool
                                                  {
                                    if (auto *r = GetGlobalSystem<RendererSystem>())
                                        r->GetScene().SetNodeScript(node, path);
                                    return true; },
                                                  {".lua"});
                            }
                        }
                        ui::ItemTooltip("Choose an existing Lua script asset.");
                        if (ImGui::MenuItem("New Empty Script"))
                        {
                            if (auto *se = m_gui->GetWidget<ScriptEditor>())
                                se->OpenNewScript(node);
                        }
                        ui::ItemTooltip("Create a new Lua script and attach it to this node.");
                        ImGui::EndMenu();
                    }
                }

                if (!(flags & Component_Mesh))
                {
                    const bool meshMenuOpen = ImGui::BeginMenu("Mesh");
                    ui::ItemTooltip("Add a mesh component using a built-in primitive.");
                    if (meshMenuOpen)
                    {
                        for (const auto &p : kPrimitiveMenu)
                        {
                            if (ImGui::MenuItem(p.name))
                                attachPrimitive(node, p.make());
                            ui::ItemTooltip(p.tip);
                        }
                        ImGui::EndMenu();
                    }
                }

#ifdef PE_PHYSICS
                if (!(flags & Component_Physics))
                {
                    if (ImGui::MenuItem("Physics Body"))
                    {
                        if (auto *ps = GetGlobalSystem<PhysicsSystem>())
                        {
                            PhysicsBodyDesc desc;
                            ps->AddBody(scene, node, desc);
                        }
                    }
                    ui::ItemTooltip("Add a 3D physics body component.");
                }
#endif

#ifdef PE_PHYSICS2D
                if (!(flags & Component_Physics2D))
                {
                    if (ImGui::MenuItem("Physics2D Body"))
                    {
                        if (auto *physics2d = GetGlobalSystem<Physics2DSystem>())
                        {
                            Physics2DBodyDesc desc;
                            physics2d->AddBody(scene, node, desc);
                        }
                    }
                    ui::ItemTooltip("Add a 2D Box2D physics body component.");
                }
#endif

                if (!(flags & Component_RuntimeUi))
                {
                    if (ImGui::MenuItem("Runtime UI"))
                        scene.AddComponentFlag(node, Component_RuntimeUi);
                    ui::ItemTooltip("Tag this node as a Runtime UI surface.");
                }

                if (!(flags & Component_Sprite))
                {
                    if (ImGui::MenuItem("Sprite"))
                        scene.AddComponentFlag(node, Component_Sprite);
                    ui::ItemTooltip("Tag this mesh node as a sprite authoring surface.");
                }

                ImGui::EndPopup();
            }

            // Mirrors the add menu: only the components actually on this node are listed.
            if (ImGui::BeginPopup("RemoveComponentPopup"))
            {
                auto notifyChanged = [&]()
                {
                    if (m_gui)
                        m_gui->NotifyChange();
                };

#ifdef PE_AUDIO
                if (flags & Component_Audio)
                {
                    if (ImGui::MenuItem("Audio Source"))
                    {
                        if (auto *as = GetGlobalSystem<AudioSystem>())
                            as->RemoveSource(node);
                        notifyChanged();
                    }
                    ui::ItemTooltip("Remove the audio source component from this node.");
                }
#endif

                if (flags & Component_Script)
                {
                    if (ImGui::MenuItem("Lua Script"))
                    {
                        scene.SetNodeScript(node, "");
                        notifyChanged();
                    }
                    ui::ItemTooltip("Detach the Lua script from this node.");
                }

                if (flags & Component_Mesh)
                {
                    if (ImGui::MenuItem("Mesh"))
                    {
                        scene.SetMeshRef(node, -1);
                        notifyChanged();
                    }
                    ui::ItemTooltip("Remove every mesh slot from this node.");
                }

#ifdef PE_PHYSICS
                if (flags & Component_Physics)
                {
                    if (ImGui::MenuItem("Physics Body"))
                    {
                        if (auto *ps = GetGlobalSystem<PhysicsSystem>())
                            ps->RemoveBody(node);
                        notifyChanged();
                    }
                    ui::ItemTooltip("Remove the 3D physics body from this node.");
                }
#endif

#ifdef PE_PHYSICS2D
                if (flags & Component_Physics2D)
                {
                    if (ImGui::MenuItem("Physics2D Body"))
                    {
                        if (auto *physics2d = GetGlobalSystem<Physics2DSystem>())
                            physics2d->RemoveBody(node);
                        notifyChanged();
                    }
                    ui::ItemTooltip("Remove the 2D physics body from this node.");
                }
#endif

                if (flags & Component_RuntimeUi)
                {
                    if (ImGui::MenuItem("Runtime UI"))
                    {
                        scene.RemoveComponentFlag(node, Component_RuntimeUi);
                        notifyChanged();
                    }
                    ui::ItemTooltip("Remove the Runtime UI tag from this node.");
                }

                if (flags & Component_Sprite)
                {
                    if (ImGui::MenuItem("Sprite"))
                    {
                        scene.ClearSpriteComponent(node);
                        notifyChanged();
                    }
                    ui::ItemTooltip("Remove the sprite authoring tag. The mesh and material remain unchanged.");
                }

                ImGui::EndPopup();
            }
        };

        auto drawSkyboxComponent = [&](NodeId *node)
        {
            NodeSkyboxTag *skybox = scene.GetSkyboxForNode(node);
            if (!skybox)
                return;

            auto applyPath = [&](std::string path)
            {
                scene.SetSkyboxPath(node, std::move(path));
                if (m_gui)
                    m_gui->NotifyChange();
            };

            auto drawRow = [&]()
            {
                const std::string &path = skybox->path;
                ImGui::PushID("SkyboxPath");

                const ImGuiStyle &style = ImGui::GetStyle();
                const float buttonSize = ImGui::GetFrameHeight();
                const float clearWidth = ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f;
                const float labelWidth = ImGui::CalcTextSize("Path").x;
                const float actionWidth = buttonSize * 2.0f + clearWidth + style.ItemInnerSpacing.x * 2.0f;
                const float pathWidth =
                    std::max(48.0f,
                             ImGui::GetContentRegionAvail().x -
                                 labelWidth -
                                 actionWidth -
                                 style.ItemInnerSpacing.x * 4.0f);

                const float rowStartX = ImGui::GetCursorPosX();
                const float rowStartY = ImGui::GetCursorPosY();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Path");
                ImGui::SetCursorPos(ImVec2(rowStartX + labelWidth + style.ItemInnerSpacing.x, rowStartY));
                DrawSkyboxPathPreview(path, pathWidth);

                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (ui::CenteredIconButton("##Browse", ICON_FA_FOLDER, ImVec2(buttonSize, buttonSize)))
                {
                    if (auto *fs = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr)
                    {
                        fs->OpenSelection([node, this](const std::string &selectedPath) -> bool
                                          {
                            Scene *activeScene = GetActiveScene();
                            if (!activeScene || !activeScene->IsNodeAlive(node))
                                return true;

                            if (!activeScene->GetSkyboxForNode(node))
                                return true;

                            activeScene->SetSkyboxPath(node, MakeSceneSkyPathSetting(selectedPath));
                            if (m_gui)
                                m_gui->NotifyChange();
                            return true; },
                                          SkyboxExtensions(),
                                          Path::Assets + "Skyboxes");
                    }
                }
                ui::ItemTooltip("Choose an HDR or image file for the skybox.");

                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (path.empty())
                    ImGui::BeginDisabled();
                if (ImGui::Button("Clear", ImVec2(clearWidth, buttonSize)))
                    applyPath({});
                if (path.empty())
                    ImGui::EndDisabled();
                ui::ItemTooltip("Clear the skybox texture and use the solid-color fallback.", ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled);

                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (ui::CenteredIconButton("##Default", ICON_FA_ROTATE_LEFT, ImVec2(buttonSize, buttonSize)))
                    applyPath(SceneSettings::DefaultSkyboxPath);
                ui::ItemTooltip("Restore the engine's default skybox path.");
                ImGui::PopStyleVar();
                ImGui::PopID();
            };

            drawRow();
        };

        // Set when the selected node can receive a .lua script drop;
        // consumed after the switch to place the whole-window drop target.
        NodeId *scriptDropNode = nullptr;
#ifdef PE_AUDIO
        NodeId *audioDropNode = nullptr;
#endif

        switch (viewType)
        {
        case SelectionType::Node:
        {
            NodeId *node = viewNode;
            if (!node)
                break;

            uint32_t flags = scene.GetComponentFlags(node);
            if (!(flags & Component_Skybox))
                scriptDropNode = node;
#ifdef PE_AUDIO
            if (!(flags & Component_Skybox))
                audioDropNode = node;
#endif

            if (flags & Component_Skybox)
            {
                const bool skyboxOpen = ImGui::CollapsingHeader("Skybox Component", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Edit the scene skybox attached to this node.");
                if (skyboxOpen)
                {
                    ImGui::Indent(8.f);
                    drawSkyboxComponent(node);
                    ImGui::Unindent(8.f);
                }
                break;
            }

            if (flags & Component_SceneSettings)
            {
                if (ImGui::CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(8.f);
                    bool changed = DrawSceneSettingsControls();
                    ImGui::SeparatorText("Post Processing (scene default)");
                    ui::ItemTooltip("Default post-process profile, used when the camera is in no post-process zone.");
                    changed |= DrawPostProcessControls(static_cast<PostProcessProfile &>(Settings::Get<SceneSettings>()));
                    if (changed)
                        scene.MarkDirty();
                    ImGui::Unindent(8.f);
                }
                break;
            }

            // Transform is always shown for scene objects with spatial meaning
            drawTransform();

            // Components built into the node type sit above the bar; everything the bar can add or
            // remove sits below it.
            if (flags & Component_Camera)
            {
                ImGui::Separator();
                Camera *cam = scene.GetCameraForNode(node);
                if (cam)
                {
                    if (auto *w = m_gui->GetWidget<CameraWidget>())
                        w->DrawEmbed(cam);
                }
            }

            if (flags & Component_Light)
            {
                ImGui::Separator();
                auto [lt, idx] = scene.GetLightForNode(node);
                if (idx >= 0)
                {
                    const bool lightOpen = ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen);
                    ui::ItemTooltip("Edit the light attached to this node.");
                    if (lightOpen)
                    {
                        ImGui::Indent(8.f);
                        if (auto *w = m_gui->GetWidget<LightWidget>())
                            w->DrawEmbed(&scene, lt, idx);
                        ImGui::Unindent(8.f);
                    }
                }
            }

            drawComponentBar(node);
            // A menu action may have added or removed a component this frame — re-read before the sections.
            flags = scene.GetComponentFlags(node);

            if (flags & Component_TriggerZone)
            {
                ImGui::Separator();
                const bool zoneOpen = ImGui::CollapsingHeader("Trigger Zone", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("A bounded box (yellow in the viewport) that drives a script (on_enter/on_exit), "
                                "post-process, and/or audio while the camera is inside. Toggle each section on.");
                if (zoneOpen)
                {
                    ImGui::Indent(8.f);
                    if (NodeTriggerZoneTag *z = scene.GetTriggerZoneForNode(node))
                    {
                        bool changed = false;

                        // --- common ---
                        changed |= ImGui::DragFloat("Priority", &z->priority, 0.1f);
                        ui::ItemTooltip("When zones overlap, higher priority wins (post-process / audio).");
                        changed |= ImGui::SliderFloat("Blend", &z->blend, 0.0f, 1.0f);
                        ui::ItemTooltip("Master weight (0..1) for this zone's blended effects (post-process / audio).");
                        changed |= ImGui::DragFloat("Blend Distance", &z->blend_distance, 0.05f, 0.0f, 1e6f);
                        ui::ItemTooltip("World-unit fade OUTSIDE the box: full at the wall, fading to none this many "
                                        "metres away. 0 = hard edge.");
                        const char *runModes[] = {"Editor", "Player", "Both"};
                        int runModeIdx = static_cast<int>(z->runMode);
                        if (ImGui::Combo("Mode", &runModeIdx, runModes, IM_ARRAYSIZE(runModes)))
                        {
                            z->runMode = static_cast<TriggerRunMode>(runModeIdx);
                            changed = true;
                        }
                        ui::ItemTooltip("Where the Script section fires: Editor (while editing), Player (play mode / "
                                        "built game), or Both.");
                        const char *shapes[] = {"Box", "Sphere"};
                        int shapeIdx = static_cast<int>(z->shape);
                        if (ImGui::Combo("Shape", &shapeIdx, shapes, IM_ARRAYSIZE(shapes)))
                        {
                            z->shape = static_cast<ZoneShape>(shapeIdx);
                            changed = true;
                        }
                        ui::ItemTooltip("Bounds shape, shared by every section: the distance falloff, the viewport "
                                        "gizmo, and the Physics collider all use it.");
                        {
                            const mat4 &w = scene.GetWorldMatrix(node);
                            const vec3 size(glm::length(vec3(w[0])), glm::length(vec3(w[1])), glm::length(vec3(w[2])));
                            if (z->shape == ZoneShape::Sphere)
                                ImGui::TextDisabled("Bounds: radius %.2f", std::max({size.x, size.y, size.z}) * 0.5f);
                            else
                                ImGui::TextDisabled("Bounds: %.2f x %.2f x %.2f", size.x, size.y, size.z);
                            ui::ItemTooltip("Size in world units from the node's transform scale. Move/scale the node "
                                            "(yellow gizmo in the viewport) to position the zone.");
                        }

                        // --- Script section (trigger behavior only; the Lua Script is a separate node component) ---
                        if (ImGui::CollapsingHeader("Script##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zonescript", &z->scriptEnabled);
                            ui::ItemTooltip("On enter/exit, call the named functions in the zone's OWN Lua script "
                                            "(separate from any plain Script Component on this node).");
                            if (z->scriptEnabled)
                            {
                                changed |= ImGui::Checkbox("Fire for camera", &z->fireForCamera);
                                ui::ItemTooltip("Fire when the active camera crosses the box.");
                                char enterBuf[128];
                                char exitBuf[128];
                                std::snprintf(enterBuf, sizeof(enterBuf), "%s", z->onEnter.c_str());
                                std::snprintf(exitBuf, sizeof(exitBuf), "%s", z->onExit.c_str());
                                if (ImGui::InputText("On Enter", enterBuf, sizeof(enterBuf)))
                                {
                                    z->onEnter = enterBuf;
                                    changed = true;
                                }
                                ui::ItemTooltip("Script function called on enter (default on_enter).");
                                if (ImGui::InputText("On Exit", exitBuf, sizeof(exitBuf)))
                                {
                                    z->onExit = exitBuf;
                                    changed = true;
                                }
                                ui::ItemTooltip("Script function called on exit (default on_exit).");

                                // The zone's own script (separate from the node's plain Component_Script).
                                ImGui::Spacing();
                                ImGui::SeparatorText("Zone Script");
                                if (z->scriptPath.empty())
                                    ImGui::TextDisabled("(no script)");
                                else
                                    ImGui::TextWrapped("%s", z->scriptPath.c_str());
                                if (ImGui::SmallButton("Select Script##zonescript"))
                                {
                                    if (auto *fs = m_gui->GetWidget<FileSelector>())
                                        fs->OpenSelection(
                                            [node](const std::string &path) -> bool
                                            {
                                                if (auto *r = GetGlobalSystem<RendererSystem>())
                                                {
                                                    if (auto *zz = r->GetScene().GetTriggerZoneForNode(node))
                                                        zz->scriptPath = path;
                                                    r->GetScene().MarkDirty();
                                                }
                                                return true;
                                            },
                                            {".lua"});
                                }
                                ui::ItemTooltip("Choose the .lua this zone runs (its on_enter/on_exit).");
                                if (z->scriptPath.empty())
                                {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Create Script##zonescript"))
                                        if (auto *se = m_gui->GetWidget<ScriptEditor>())
                                            se->OpenNewScriptForPath(
                                                "zone_script", kZoneScriptTemplate,
                                                [node](const std::string &path)
                                                {
                                                    if (auto *r = GetGlobalSystem<RendererSystem>())
                                                    {
                                                        if (auto *zz = r->GetScene().GetTriggerZoneForNode(node))
                                                            zz->scriptPath = path;
                                                        r->GetScene().MarkDirty();
                                                    }
                                                });
                                    ui::ItemTooltip("Create a new .lua pre-filled with an on_enter/on_exit example.");
                                }
                                else
                                {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Edit##zonescript"))
                                        if (auto *se = m_gui->GetWidget<ScriptEditor>())
                                            se->OpenScriptFile(z->scriptPath);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Remove##zonescript"))
                                    {
                                        z->scriptPath.clear();
                                        changed = true;
                                    }
                                }
                            }
                        }

                        // --- Post Process section ---
                        if (ImGui::CollapsingHeader("Post Process##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zonepp", &z->postProcessEnabled);
                            ui::ItemTooltip("Make these post-process settings the active profile (blended over the scene "
                                            "default) while the camera is inside the zone.");
                            if (z->postProcessEnabled)
                            {
                                ImGui::Separator();
                                changed |= DrawPostProcessControls(z->postProcess);
                            }
                        }

                        // --- Audio section (the zone owns its OWN source, separate from any plain Audio Source) ---
                        if (ImGui::CollapsingHeader("Audio##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zoneaudio", &z->audioEnabled);
                            ui::ItemTooltip("Play this zone's own audio source while the camera is inside, blending its "
                                            "volume in over Blend Distance (0 outside the band -> Blend inside); stops "
                                            "outside. This source is the zone's; a plain Audio Source component is separate.");
                            AudioSourceDesc &a = z->audioSource;
                            std::string disp = a.filePath.empty() ? "(none)" : a.filePath;
                            if (auto sl = disp.find_last_of("/\\"); sl != std::string::npos)
                                disp = disp.substr(sl + 1);
                            ImGui::Text("File: %s", disp.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Browse##zoneaudio"))
                            {
                                if (auto *fs = m_gui->GetWidget<FileSelector>())
                                {
                                    fs->OpenSelection(
                                        [node](const std::string &path) -> bool
                                        {
                                            if (auto *r = GetGlobalSystem<RendererSystem>())
                                            {
                                                if (auto *zz = r->GetScene().GetTriggerZoneForNode(node))
                                                    zz->audioSource.filePath = path;
                                                r->GetScene().MarkDirty();
                                            }
                                            return true;
                                        },
                                        {".wav", ".mp3", ".flac", ".ogg"});
                                }
                            }
                            ui::ItemTooltip("Choose the audio file this zone plays.");
                            changed |= ImGui::DragFloat("Volume##zoneaudio", &a.volume, 0.01f, 0.0f, 2.0f);
                            changed |= ImGui::DragFloat("Pitch##zoneaudio", &a.pitch, 0.01f, 0.1f, 3.0f);
                            changed |= ImGui::Checkbox("Loop##zoneaudio", &a.loop);
                            changed |= ImGui::Checkbox("Spatial##zoneaudio", &a.spatial);
                            ui::ItemTooltip("Attenuate by listener distance (left exactly as you set it).");
                        }

                        // --- Physics section (zone becomes a Jolt sensor/collider during play; OWN script) ---
                        if (ImGui::CollapsingHeader("Physics##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zonephysics", &z->physicsEnabled);
                            ui::ItemTooltip("During play, register the zone (using the common Shape) as a physics body. "
                                            "Sensor = pass-through trigger that fires this section's own script; Solid = "
                                            "collider that blocks bodies. Uses the node's physics body — don't add a separate one.");
                            if (z->physicsEnabled)
                            {
                                bool zone2D = false;
#ifdef PE_PHYSICS2D
                                const char *engines[] = {"Physics (3D / Jolt)", "Physics2D (Box2D)"};
                                int engIdx = static_cast<int>(z->physicsEngine);
                                if (ImGui::Combo("Engine##zonephysics", &engIdx, engines, IM_ARRAYSIZE(engines)))
                                {
                                    z->physicsEngine = static_cast<ZonePhysicsEngine>(engIdx);
                                    changed = true;
                                }
                                ui::ItemTooltip("Which physics world this zone uses. 3D = Jolt, 2D = Box2D (XY plane). "
                                                "They don't mix — pick the one your scene uses. Sphere shape -> a 2D circle.");
                                zone2D = z->physicsEngine == ZonePhysicsEngine::Physics2D;
#endif
                                const char *modes[] = {"Sensor (trigger)", "Solid (collider)"};
                                int modeIdx = static_cast<int>(z->physicsMode);
                                if (ImGui::Combo("Physics Mode", &modeIdx, modes, IM_ARRAYSIZE(modes)))
                                {
                                    z->physicsMode = static_cast<ZonePhysicsMode>(modeIdx);
                                    changed = true;
                                }
                                ui::ItemTooltip("Sensor: bodies pass through and fire the Physics script. Solid: bodies collide.");

                                const char *bodyTypes[] = {"Static", "Dynamic", "Kinematic"};
                                int btIdx = static_cast<int>(z->physicsBodyType);
                                if (ImGui::Combo("Body Type", &btIdx, bodyTypes, IM_ARRAYSIZE(bodyTypes)))
                                {
                                    z->physicsBodyType = static_cast<PhysicsBodyType>(btIdx);
                                    changed = true;
                                }
                                ui::ItemTooltip("Static (doesn't move), Dynamic (falls/reacts to forces), or Kinematic "
                                                "(moved by script/animation).");

                                if (z->physicsMode == ZonePhysicsMode::Solid ||
                                    z->physicsBodyType == PhysicsBodyType::Dynamic)
                                {
                                    // 2D bodies are sized by area density rather than absolute mass.
                                    changed |= ImGui::DragFloat(zone2D ? "Density##zonephysics" : "Mass##zonephysics",
                                                                &z->physicsMass, 0.05f, 0.0f, 1e6f);
                                    changed |= ImGui::DragFloat("Friction##zonephysics", &z->physicsFriction, 0.01f, 0.0f, 1.0f);
                                    changed |= ImGui::DragFloat("Restitution##zonephysics", &z->physicsRestitution, 0.01f, 0.0f, 1.0f);
                                    ui::ItemTooltip("Restitution = bounciness (0 = none, 1 = full).");
                                }

                                if (z->physicsMode == ZonePhysicsMode::Sensor)
                                {
                                    char filterBuf[128];
                                    std::snprintf(filterBuf, sizeof(filterBuf), "%s", z->physicsFilterTag.c_str());
                                    if (ImGui::InputText("Filter##zonephysics", filterBuf, sizeof(filterBuf)))
                                    {
                                        z->physicsFilterTag = filterBuf;
                                        changed = true;
                                    }
                                    ui::ItemTooltip("Only bodies whose node name contains this fire the trigger (empty = any).");

                                    char enterBuf[128];
                                    char exitBuf[128];
                                    std::snprintf(enterBuf, sizeof(enterBuf), "%s", z->physicsOnEnter.c_str());
                                    std::snprintf(exitBuf, sizeof(exitBuf), "%s", z->physicsOnExit.c_str());
                                    if (ImGui::InputText("On Enter##zonephysics", enterBuf, sizeof(enterBuf)))
                                    {
                                        z->physicsOnEnter = enterBuf;
                                        changed = true;
                                    }
                                    if (ImGui::InputText("On Exit##zonephysics", exitBuf, sizeof(exitBuf)))
                                    {
                                        z->physicsOnExit = exitBuf;
                                        changed = true;
                                    }
                                    ui::ItemTooltip("Functions called in the Physics script on overlap enter/exit.");

                                    // Force field: push bodies inside the sensor every frame (layered on the sensor).
                                    changed |= ImGui::Checkbox("Force Field##zoneff", &z->physicsForceField);
                                    ui::ItemTooltip("While a body overlaps this sensor, push it every frame by the force "
                                                    "below (world units). E.g. (0, 20, 0) = an anti-gravity updraft.");
                                    if (z->physicsForceField)
                                    {
                                        changed |= ImGui::DragFloat3("Force##zoneff", &z->physicsForce.x, 0.5f);
                                        ui::ItemTooltip("World-space force applied per frame to bodies inside. 2D ignores Z.");
                                    }

                                    // The Physics section's OWN script — separate from the Script section's.
                                    ImGui::Spacing();
                                    ImGui::SeparatorText("Physics Script");
                                    if (z->physicsScriptPath.empty())
                                        ImGui::TextDisabled("(no script)");
                                    else
                                        ImGui::TextWrapped("%s", z->physicsScriptPath.c_str());
                                    if (ImGui::SmallButton("Select Script##zonephysicsscript"))
                                    {
                                        if (auto *fs = m_gui->GetWidget<FileSelector>())
                                            fs->OpenSelection(
                                                [node](const std::string &path) -> bool
                                                {
                                                    if (auto *r = GetGlobalSystem<RendererSystem>())
                                                    {
                                                        if (auto *zz = r->GetScene().GetTriggerZoneForNode(node))
                                                            zz->physicsScriptPath = path;
                                                        r->GetScene().MarkDirty();
                                                    }
                                                    return true;
                                                },
                                                {".lua"});
                                    }
                                    ui::ItemTooltip("Choose the .lua this zone's physics trigger runs (separate from the Script section).");
                                    if (z->physicsScriptPath.empty())
                                    {
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Create Script##zonephysicsscript"))
                                            if (auto *se = m_gui->GetWidget<ScriptEditor>())
                                                se->OpenNewScriptForPath(
                                                    "zone_physics", kZonePhysicsScriptTemplate,
                                                    [node](const std::string &path)
                                                    {
                                                        if (auto *r = GetGlobalSystem<RendererSystem>())
                                                        {
                                                            if (auto *zz = r->GetScene().GetTriggerZoneForNode(node))
                                                                zz->physicsScriptPath = path;
                                                            r->GetScene().MarkDirty();
                                                        }
                                                    });
                                        ui::ItemTooltip("Create a new .lua pre-filled with a physics-trigger example (commented).");
                                    }
                                    else
                                    {
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Edit##zonephysicsscript"))
                                            if (auto *se = m_gui->GetWidget<ScriptEditor>())
                                                se->OpenScriptFile(z->physicsScriptPath);
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Remove##zonephysicsscript"))
                                        {
                                            z->physicsScriptPath.clear();
                                            changed = true;
                                        }
                                    }
                                }
                            }
                        }

                        // --- Spawn section (instantiate a prefab when the camera enters; despawn on exit) ---
                        if (ImGui::CollapsingHeader("Spawn##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zonespawn", &z->spawnEnabled);
                            ui::ItemTooltip("Instantiate a prefab at the zone when the camera enters; remove it on exit. "
                                            "Re-entry only re-spawns once the previous copy is gone (no duplicates).");
                            if (z->spawnEnabled)
                            {
                                std::string disp = z->spawnPrefabPath.empty() ? "(none)" : z->spawnPrefabPath;
                                if (auto sl = disp.find_last_of("/\\"); sl != std::string::npos)
                                    disp = disp.substr(sl + 1);
                                ImGui::Text("Prefab: %s", disp.c_str());
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Browse##zonespawn"))
                                {
                                    if (auto *fs = m_gui->GetWidget<FileSelector>())
                                        fs->OpenSelection(
                                            [node](const std::string &path) -> bool
                                            {
                                                if (auto *r = GetGlobalSystem<RendererSystem>())
                                                {
                                                    if (auto *zz = r->GetScene().GetTriggerZoneForNode(node))
                                                        zz->spawnPrefabPath = path;
                                                    r->GetScene().MarkDirty();
                                                }
                                                return true;
                                            },
                                            {".peprefab"});
                                }
                                ui::ItemTooltip("Choose the .peprefab to spawn at this zone.");
                                changed |= ImGui::Checkbox("Despawn On Exit##zonespawn", &z->spawnDespawnOnExit);
                                ui::ItemTooltip("Remove the spawned instance when the camera leaves. Off = it stays (one copy).");
                            }
                        }

                        // --- Streaming section (enable a named subtree while inside; disable outside) ---
                        if (ImGui::CollapsingHeader("Streaming##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zonestream", &z->streamEnabled);
                            ui::ItemTooltip("Enable a target node (by name) while the camera is inside, disable it outside. "
                                            "Author heavy region content disabled, then light it up only when near.");
                            if (z->streamEnabled)
                            {
                                char tgtBuf[128];
                                std::snprintf(tgtBuf, sizeof(tgtBuf), "%s", z->streamTargetName.c_str());
                                if (ImGui::InputText("Target Node##zonestream", tgtBuf, sizeof(tgtBuf)))
                                {
                                    z->streamTargetName = tgtBuf;
                                    changed = true;
                                }
                                ui::ItemTooltip("Exact name of the node (subtree) to enable while inside / disable outside.");
                            }
                        }

                        // --- Camera section (activate a camera node and/or override FOV while inside) ---
                        if (ImGui::CollapsingHeader("Camera##zone"))
                        {
                            changed |= ImGui::Checkbox("Enabled##zonecam", &z->cameraEnabled);
                            ui::ItemTooltip("While inside: switch to a named camera node and/or override the active camera's "
                                            "FOV. Restores the previous camera + FOV on exit.");
                            if (z->cameraEnabled)
                            {
                                char camBuf[128];
                                std::snprintf(camBuf, sizeof(camBuf), "%s", z->cameraTargetName.c_str());
                                if (ImGui::InputText("Camera Node##zonecam", camBuf, sizeof(camBuf)))
                                {
                                    z->cameraTargetName = camBuf;
                                    changed = true;
                                }
                                ui::ItemTooltip("Exact name of a camera node to activate (leave empty to only override FOV).");
                                changed |= ImGui::DragFloat("FOV (deg)##zonecam", &z->cameraFovDeg, 0.5f, 0.0f, 179.0f);
                                ui::ItemTooltip("Override the active camera's horizontal FOV in degrees while inside (0 = no change).");
                            }
                        }

                        if (changed)
                            scene.MarkDirty();
                    }
                    ImGui::Unindent(8.f);
                }
            }

            if (flags & Component_Sprite)
            {
                ImGui::Separator();
                const bool spriteOpen = ImGui::CollapsingHeader("Sprite Component", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Inspect the sprite authoring metadata attached to this node.");
                if (spriteOpen)
                {
                    ImGui::Indent(8.f);
                    drawSpriteComponent(node);
                    ImGui::Unindent(8.f);
                }
            }

            if (flags & Component_VoxelWorld)
            {
                ImGui::Separator();
                const bool voxOpen = ImGui::CollapsingHeader("Voxel World", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("All voxel-world settings. The world is created and kept in sync with these "
                                "values live (editor and play); the node's position is the volume center for "
                                "bounded or non-streaming worlds.");
                if (voxOpen)
                {
                    ImGui::Indent(8.f);
                    if (NodeVoxelWorldTag *v = scene.GetVoxelWorldForNode(node))
                    {
                        bool changed = false;
                        changed |= ImGui::Checkbox("Enabled##voxelworld", &v->worldEnabled);
                        ui::ItemTooltip("Build and keep the voxel world while this node is enabled.");
                        changed |= ImGui::Checkbox("Streaming", &v->streaming);
                        ui::ItemTooltip("Stream columns around the anchor (camera/player). Off = load a fixed "
                                        "grid at this node once (World Radius, or Load Radius when 0) and never "
                                        "unload.");
                        if (v->streaming)
                        {
                            changed |= ImGui::Checkbox("Anchor Follows Camera", &v->anchorFollowsCamera);
                            ui::ItemTooltip("Stream around the active camera. Off: a script drives "
                                            "voxel.set_anchor (e.g. around the player).");
                            changed |= ImGui::DragInt("Load Radius", &v->loadRadius, 0.2f, 1, 64);
                            ui::ItemTooltip("Streaming radius in 16 m columns around the anchor.");
                            changed |= ImGui::DragInt("Unload Margin", &v->unloadMargin, 0.2f, 0, 16);
                            ui::ItemTooltip("Extra columns kept loaded past the radius before unloading.");
                        }
                        changed |= ImGui::DragInt("World Radius", &v->worldRadius, 0.2f, 0, 4096);
                        ui::ItemTooltip("Total world size in columns around this node; columns outside never "
                                        "generate. 0 = infinite in X/Z.");
                        changed |= ImGui::DragInt("Ground Y", &v->groundY, 0.5f, 0, 256);
                        ui::ItemTooltip("Terrain base height fed to the world generator (noise base / sea-level datum).");
                        changed |= ImGui::DragInt("Upload Budget", &v->uploadBudget, 0.2f, 1, 64);
                        ui::ItemTooltip("Section meshes uploaded per frame; higher streams in faster but can "
                                        "spike frame time.");
                        changed |= ImGui::Checkbox("LOD", &v->lodEnabled);
                        ui::ItemTooltip("Distance LOD: full detail near the anchor, coarser mesh per band "
                                        "beyond. Retuned live without rebuilding the world.");
                        if (v->lodEnabled)
                        {
                            ImGui::Indent(16.0f);
                            ImGui::SetNextItemWidth(120.0f);
                            changed |= ImGui::DragInt("Full-Detail Radius", &v->lod0Radius, 0.2f, 1, 64);
                            ui::ItemTooltip("Full-detail ring in columns, measured 3D from the camera "
                                            "(circular; height counts — a high camera coarsens everything). "
                                            "2 m cells out to 3x this radius, 4 m cells beyond.");
                            ImGui::Unindent(16.0f);
                        }
                        char saveBuf[260];
                        snprintf(saveBuf, sizeof(saveBuf), "%s", v->saveDir.c_str());
                        if (ImGui::InputText("Save Dir", saveBuf, sizeof(saveBuf)))
                        {
                            v->saveDir = saveBuf;
                            changed = true;
                        }
                        ui::ItemTooltip("Column persistence dir under Assets (edits save as .pevcol overlays). "
                                        "Empty = no persistence. Changing it rebuilds the world.");

                        ImGui::SeparatorText("World Generation");
                        // Height Range + Ground Height map a heightmap's 0..1 value into block height
                        // (MapGen::MapHeight). Heightmap terrain only; noise cube terrain uses Ground Y.
                        if (!v->heightmapPath.empty())
                        {
                            changed |= ImGui::DragFloatRange2("Height Range (m)", &v->heightMin, &v->heightMax, 0.5f,
                                                              -256.0f, 256.0f, "min %.0f", "max %.0f");
                            ui::ItemTooltip("Vertical span in metres around Ground Height the heightmap maps into: "
                                            "0 = Ground Height, 1 = Ground Height + max.");
                            v->groundHeight = std::clamp(v->groundHeight, v->heightMin, v->heightMax);
                            changed |= ImGui::DragFloat("Ground Height", &v->groundHeight, 0.25f, v->heightMin,
                                                        v->heightMax, "%.1f");
                            ui::ItemTooltip("Height in metres the 0 map value sits at; drags within Height Range.");
                        }
                        auto pathField = [&changed](const char *label, std::string &path)
                        {
                            char buf[260];
                            snprintf(buf, sizeof(buf), "%s", path.c_str());
                            if (ImGui::InputText(label, buf, sizeof(buf)))
                            {
                                path = buf;
                                changed = true;
                            }
                        };
                        pathField("Heightmap", v->heightmapPath);
                        ui::ItemTooltip("Signed height map under Assets (paint it in Map Painter): value -1..1 is a "
                                        "height scaler (0 = ground) mapped into Height Range, lerped between pixels, "
                                        "centered on this node. Empty = procedural noise terrain.");
                        if (v->heightmapPath.empty())
                        {
                            changed |= ImGui::DragFloat("Mountain Height", &v->noiseAmplitude, 0.25f, 0.0f, 128.0f, "%.0f");
                            ui::ItemTooltip("Peak height above Ground Y in metres. 0 = flat plain.");
                            changed |= ImGui::DragFloat("Feature Scale", &v->noiseFeatureScale, 0.5f, 8.0f, 1024.0f,
                                                        "%.0f");
                            ui::ItemTooltip("Terrain feature wavelength in metres — small = choppy hills, large = "
                                            "wide rolling terrain.");
                            changed |= ImGui::DragInt("Seed", &v->noiseSeed);
                            ui::ItemTooltip("Shifts the noise domain; each seed is a different world.");
                            changed |= ImGui::Checkbox("Caves", &v->caves);
                            ui::ItemTooltip("Carve worm caves under the surface.");
                        }
                        else
                        {
                            pathField("Strata 1 Map", v->strata1Path);
                            ui::ItemTooltip("Grayscale thickness (m) of the band under the surface block. "
                                            "Empty = fixed Strata 1 Thickness.");
                            pathField("Strata 2 Map", v->strata2Path);
                            ui::ItemTooltip("Grayscale thickness (m) of the band under strata 1. Empty = "
                                            "fixed Strata 2 Thickness.");
                            pathField("Features Map", v->featuresPath);
                            ui::ItemTooltip("Decoration map: pixel 1 = tree, 2 = rock at that pixel's block. "
                                            "Paint it sparse in the Map Painter's Features layer.");
                            changed |= ImGui::DragInt("Meters / Pixel", &v->blocksPerPixel, 0.1f, 1, 64);
                            ui::ItemTooltip("Metres each map pixel spans in X/Z; heights lerp between pixels.");
                            changed |= ImGui::Checkbox("Elevation Bands", &v->surfaceBands);
                            ui::ItemTooltip("Pick the top block by height (sand < dry grass < rock < snow) instead of "
                                            "one Surface Block. Great for coast-to-summit terrain.");
                            changed |= ImGui::DragInt("Surface Block", &v->surfaceBlock, 0.1f, 0, 255);
                            ui::ItemTooltip("Top block when Elevation Bands is off. Ids: 1=stone 2=dirt 3=grass "
                                            "4=water 8=sand 10=rock 11=snow 13=marble, 0=air.");
                            changed |= ImGui::DragInt("Strata 1 Block", &v->strata1Block, 0.1f, 0, 255);
                            changed |= ImGui::DragInt("Strata 1 Thickness", &v->strata1Thickness, 0.2f, 0, 128);
                            changed |= ImGui::DragInt("Strata 2 Block", &v->strata2Block, 0.1f, 0, 255);
                            changed |= ImGui::DragInt("Strata 2 Thickness", &v->strata2Thickness, 0.2f, 0, 128);
                            changed |= ImGui::DragInt("Fill Block", &v->fillBlock, 0.1f, 0, 255);
                            ui::ItemTooltip("Fills below the strata down to y=0. 0 = air (floating-island "
                                            "shells).");
                        }
                        changed |= ImGui::DragInt("Sea Level", &v->seaLevel, 0.2f, -1, 256);
                        ui::ItemTooltip("Water surface height in metres: -1 = auto (Ground Y - 2), 0 = no water.");
                        changed |= ImGui::Checkbox("Auto Rebuild", &v->autoRebuild);
                        ui::ItemTooltip("On (default): worldgen edits rebuild the world automatically after a short "
                                        "settle. Off: edits only apply when you press Rebuild World — for batching "
                                        "expensive smooth-terrain edits.");
                        if (ImGui::Button("Rebuild World"))
                            v->rebuildRequested = true;
                        ui::ItemTooltip("Force-rebuild now — applies staged edits (needed after repainting a map "
                                        "file, or any edit while Auto Rebuild is off).");
                        ImGui::TextDisabled("Block = 1 m, section = 16^3 m (engine constants)");
                        if (changed)
                            scene.MarkDirty();
                    }
                    ImGui::Unindent(8.f);
                }
            }

            // Terrain (streamed isosurface) component
            if (flags & Component_Terrain)
            {
                ImGui::Separator();
                const bool terrOpen = ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Smooth terrain from a heightmap or noise, sculptable in 3D (terrain.sculpt). Kept in "
                                "sync with these values live; the node's position is the terrain center.");
                if (terrOpen)
                {
                    ImGui::Indent(8.f);
                    if (NodeTerrainTag *t = scene.GetTerrainForNode(node))
                    {
                        bool changed = false;
                        changed |= ImGui::Checkbox("Enabled##terrain", &t->worldEnabled);
                        ui::ItemTooltip("Build and keep the terrain while this node is enabled.");
                        changed |= ImGui::DragInt("Size X (m)", &t->sizeXMeters, 1.0f, 0, 65536);
                        ui::ItemTooltip("Terrain width in metres, centred on this node. 0 with a heightmap fills the "
                                        "map's X extent. Mesh is capped at 2048 cells/axis — raise Meters/Pixel for a "
                                        "bigger world.");
                        changed |= ImGui::DragInt("Size Z (m)", &t->sizeZMeters, 1.0f, 0, 65536);
                        ui::ItemTooltip("Terrain depth in metres, centred on this node. 0 with a heightmap fills the "
                                        "map's Z extent.");
                        changed |= ImGui::DragFloat("Meters / Pixel", &t->metersPerPixel, 0.05f, 0.05f, 256.0f, "%.2f");
                        ui::ItemTooltip("World metres each heightmap pixel / mesh cell spans. >1 spreads a small map "
                                        "over a big world with fewer verts; <1 adds mesh detail. Cells = Size / this, "
                                        "capped at 2048/axis.");

                        changed |= ImGui::DragFloatRange2("Height Range (m)", &t->heightMin, &t->heightMax, 0.5f,
                                                          -256.0f, 256.0f, "min %.0f", "max %.0f");
                        ui::ItemTooltip("Vertical span in metres around Ground Height: a heightmap 0..1 (or the noise) "
                                        "maps into Ground Height + [min, max]. May dip below y=0.");
                        t->groundHeight = std::clamp(t->groundHeight, t->heightMin, t->heightMax);
                        changed |= ImGui::DragFloat("Ground Height", &t->groundHeight, 0.25f, t->heightMin, t->heightMax,
                                                    "%.1f m");
                        ui::ItemTooltip("World height the mid-gray level sits at; drags within Height Range.");
                        t->seaLevelM = std::clamp(t->seaLevelM, t->heightMin, t->heightMax);
                        changed |= ImGui::DragFloat("Sea Level (m)", &t->seaLevelM, 0.25f, t->heightMin, t->heightMax,
                                                    "%.1f m");
                        ui::ItemTooltip("Surface below this world height is tinted underwater (no water surface yet). "
                                        "Default 0.");

                        changed |= InputTextDeferred("Heightmap", t->heightmapPath);
                        ui::ItemTooltip("Grayscale surface map under Assets (paint it in Map Painter): 0..1 maps into "
                                        "Height Range, lerped between pixels, centered on this node. Empty = noise. "
                                        "Applies when the field loses focus.");
                        changed |= InputTextDeferred("Caves Map", t->cavesPath);
                        ui::ItemTooltip("Grayscale cave map under Assets (paint it in Map Painter's Caves layer): "
                                        "pixel value = how open the void under the surface is; caves pinch closed "
                                        "where the paint fades. Same extent/orientation as the heightmap. Reach them "
                                        "by sculpting an entrance (or via overhang mouths). Empty = none.");
                        changed |= InputTextDeferred("Scatter Map", t->scatterPath);
                        ui::ItemTooltip("Scatter map under Assets (paint it in Map Painter's Scatter layer or the "
                                        "viewport Terrain Brush): pixel value picks a Scatter Mesh below; instances "
                                        "bake into the terrain tiles (they stream, LOD and collide with the tile). "
                                        "Same extent/orientation as the heightmap. Empty = none.");
                        for (size_t si = 0; si < t->scatterMeshes.size(); ++si)
                        {
                            ImGui::PushID(static_cast<int>(si));
                            ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - 28.0f);
                            char lbl[32];
                            snprintf(lbl, sizeof(lbl), "Scatter Mesh %zu", si + 1);
                            changed |= InputTextDeferred(lbl, t->scatterMeshes[si]);
                            ui::ItemTooltip("Builtin \"tree\", \"rock\", \"grass\", or a low-poly model asset path "
                                            "under Assets. Painted pixels with this 1-based index place it.");
                            ImGui::SameLine();
                            if (ImGui::SmallButton("x"))
                            {
                                t->scatterMeshes.erase(t->scatterMeshes.begin() + si);
                                changed = true;
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                        if (t->scatterMeshes.size() < 8 && ImGui::SmallButton("+ Add Scatter Mesh"))
                        {
                            t->scatterMeshes.emplace_back("tree");
                            changed = true;
                        }

                        changed |= InputTextDeferred("Splat Map", t->splatPath);
                        ui::ItemTooltip("Optional RGBA splat map under Assets (R=grass G=rock B=sand A=snow weights). "
                                        "Empty = automatic height/slope selection. Applies when the field loses focus.");
                        static const char *kLayerLabels[4] = {"Layer 0 (grass)", "Layer 1 (rock)",
                                                              "Layer 2 (sand)", "Layer 3 (snow)"};
                        for (int li = 0; li < 4; ++li)
                            changed |= InputTextDeferred(kLayerLabels[li], t->layerPaths[li]);
                        ui::ItemTooltip("Triplanar albedo textures for the 4 splat layers (default = built-in tiles).");
                        static const char *kMatLabels[4] = {"Material 0 (grass)", "Material 1 (rock)",
                                                            "Material 2 (sand)", "Material 3 (snow)"};
                        for (int li = 0; li < 4; ++li)
                            changed |= InputTextDeferred(kMatLabels[li], t->materialPaths[li]);
                        ui::ItemTooltip("Optional per-layer material maps: RGB = tangent-space normal, A = roughness. "
                                        "Empty = flat normal + full roughness (terrain looks as before).");
                        if (ImGui::DragFloat("Texture Scale", &t->textureScaleM, 0.05f, 0.1f, 64.0f, "%.2f m/tile"))
                        {
                            t->textureScaleM = std::clamp(t->textureScaleM, 0.1f, 64.0f);
                            changed = true;
                        }
                        ui::ItemTooltip("World metres each triplanar texture tile spans (live — no rebuild). "
                                        "Bigger = coarser, more stretched; smaller = finer, more repetition.");

                        if (t->heightmapPath.empty())
                        {
                            changed |= ImGui::DragFloat("Feature Scale", &t->noiseFeatureScale, 0.5f, 8.0f, 1024.0f, "%.0f");
                            ui::ItemTooltip("Noise feature wavelength in metres — small = choppy, large = rolling.");
                            changed |= ImGui::DragInt("Seed", &t->noiseSeed);
                            ui::ItemTooltip("Each seed is a different terrain.");
                            changed |= ImGui::SliderFloat("Overhangs", &t->overhangs, 0.0f, 1.0f, "%.2f");
                            ui::ItemTooltip("3D relief strength: cliffs undercut and hollows open (true overhangs). "
                                            "0 = pure heightfield. Widens the meshing band, so it costs build time.");
                        }

                        changed |= ImGui::Checkbox("Streaming", &t->streaming);
                        ui::ItemTooltip("The terrain window (Size X/Z) follows the camera instead of being a fixed "
                                        "patch, with two coarser rings extending the view distance. Tiles re-mesh in "
                                        "place as you move — no rebuild hitches.");

                        changed |= ImGui::Checkbox("Physics Collision", &t->physics);
                        ui::ItemTooltip("Give the terrain per-tile static physics colliders so rigidbodies collide "
                                        "with it in play mode. Toggling does not rebuild the terrain.");
                        if (t->physics)
                        {
                            changed |= ImGui::DragFloat("Friction", &t->physicsFriction, 0.01f, 0.0f, 1.0f);
                            ui::ItemTooltip("Terrain surface friction — how much sliding objects grip it.");
                            changed |= ImGui::DragFloat("Restitution", &t->physicsRestitution, 0.01f, 0.0f, 1.0f);
                            ui::ItemTooltip("Terrain bounciness — how much objects rebound on impact.");
                            changed |= ImGui::DragFloat("Collision Radius (m)", &t->collisionRadiusM, 1.0f, 0.0f, 4096.0f,
                                                        "%.0f");
                            ui::ItemTooltip("Colliders exist only within this range of the camera (cook cost tracks "
                                            "the player). 0 = collide everywhere. Applied live.");
                        }

                        changed |= ImGui::Checkbox("Auto Rebuild", &t->autoRebuild);
                        ui::ItemTooltip("On: worldgen edits rebuild after a short settle. Off: only on Rebuild Terrain.");
                        if (ImGui::Button("Rebuild Terrain"))
                            t->rebuildRequested = true;
                        ui::ItemTooltip("Force-rebuild now — needed after repainting the heightmap, or any edit while "
                                        "Auto Rebuild is off.");
                        if (changed)
                            scene.MarkDirty();
                    }
                    ImGui::Unindent(8.f);
                }
            }

            // Mesh component
            if (flags & Component_Mesh)
            {
                ImGui::Separator();
                const bool meshOpen = ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Edit mesh data, materials, and textures for this node.");
                if (meshOpen)
                {
                    ImGui::Indent(8.f);
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.35f, 0.35f, 0.38f, 1.0f));
                    drawMeshComponent(node);
                    ImGui::PopStyleColor(3);
                    ImGui::Unindent(8.f);
                }
            }

            // Script component (separate node component; a Trigger Zone fires its on_enter/on_exit)
            if (flags & Component_Script)
            {
                ImGui::Separator();
                bool open = ImGui::CollapsingHeader("Script Component", ImGuiTreeNodeFlags_DefaultOpen);
                const bool scriptHeaderHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                // Drop a .lua file on the header to replace the current script
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        std::string path((const char *)p->Data, p->DataSize - 1);
                        if (std::filesystem::path(path).extension() == ".lua")
                            scene.SetNodeScript(node, path);
                    }
                    ImGui::EndDragDropTarget();
                }
                if (scriptHeaderHovered)
                    ui::TooltipText("Edit the Lua script attached to this node; drop a .lua file here to replace it.");
                if (open)
                    drawScriptComponent(node);
            }

            if (scene.NodeUsesSkinnedStrip2D(node))
            {
                ImGui::Separator();
                const bool stripOpen = ImGui::CollapsingHeader("Skinned Strip 2D", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Pose the generated 2D skinned strip.");
                if (stripOpen)
                {
                    ImGui::Indent(8.f);
                    drawSkinnedStrip2D(node);
                    ImGui::Unindent(8.f);
                }
            }

            AnimationSystem *animationSystem = GetGlobalSystem<AnimationSystem>();
            const bool showAnimationRuntime =
                animationSystem &&
                (animationSystem->GetAnimationState(node) ||
                 (scene.NodeHasSkinnedMesh(node) && !scene.GetAnimationClipsForNode(node).empty()));
            if (showAnimationRuntime)
            {
                ImGui::Separator();
                const bool animationOpen = ImGui::CollapsingHeader("Animation Runtime", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Preview and scrub animation playback on this node.");
                if (animationOpen)
                {
                    ImGui::Indent(8.f);
                    drawAnimationRuntime(node);
                    ImGui::Unindent(8.f);
                }
            }

#ifdef PE_PHYSICS
            // Physics component
            if (flags & Component_Physics)
            {
                ImGui::Separator();
                const bool physicsOpen = ImGui::CollapsingHeader("Physics Component", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Edit the 3D physics body attached to this node.");
                if (physicsOpen)
                {
                    ImGui::Indent(8.f);
                    if (auto *w = m_gui->GetWidget<PhysicsWidget>())
                        w->DrawEmbed(node, &scene);
                    ImGui::Unindent(8.f);
                }
            }
#endif

#ifdef PE_PHYSICS2D
            // Physics2D component
            if (flags & Component_Physics2D)
            {
                ImGui::Separator();
                const bool physics2DOpen = ImGui::CollapsingHeader("Physics2D Component", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Edit the 2D physics body attached to this node.");
                if (physics2DOpen)
                {
                    ImGui::Indent(8.f);
                    if (auto *w = m_gui->GetWidget<Physics2DWidget>())
                        w->DrawEmbed(node, &scene);
                    ImGui::Unindent(8.f);
                }
            }
#endif

#ifdef PE_AUDIO
            // Audio component
            // Audio Source (separate node component; a Trigger Zone plays it on enter and blends it)
            if (flags & Component_Audio)
            {
                ImGui::Separator();
                const bool audioOpen = ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Edit the audio source attached to this node.");
                if (audioOpen)
                {
                    ImGui::Indent(8.f);
                    if (auto *w = m_gui->GetWidget<AudioWidget>())
                        w->DrawEmbed(node, &scene);
                    ImGui::Unindent(8.f);
                }
            }
#endif

            if (flags & Component_RuntimeUi)
            {
                ImGui::Separator();
                const bool runtimeUiOpen = ImGui::CollapsingHeader("Runtime UI Component", ImGuiTreeNodeFlags_DefaultOpen);
                ui::ItemTooltip("Edit the Runtime UI tag attached to this node.");
                if (runtimeUiOpen)
                {
                    ImGui::Indent(8.f);
                    drawRuntimeUiComponent(node);
                    ImGui::Unindent(8.f);
                }
            }

            break;
        }
        case SelectionType::Mesh:
        {
            // Kept for backward compatibility — mesh selection can still come from code paths
            NodeId *node = viewNode;
            if (!node)
                break;
            int meshIndex = scene.GetMeshRef(node);
            if (meshIndex < 0)
                break;
            Mesh &mesh = scene.GetMesh(meshIndex);
            if (auto *w = m_gui->GetWidget<MeshWidget>())
                w->DrawEmbed(&mesh, node);
            break;
        }
        case SelectionType::Camera:
            drawCamera();
            break;
        case SelectionType::Light:
            drawLight();
            break;
        case SelectionType::Emitter:
            drawEmitter();
            break;
        default:
            break;
        }

        // Whole-window .lua script drop target (active whenever a node that can
        // hold a script is selected, regardless of what's under the cursor)
        if (scriptDropNode)
        {
            // Draw a green border while a .lua file is being dragged
            const ImGuiPayload *drag = ImGui::GetDragDropPayload();
            if (drag && strcmp(drag->DataType, "CONTENT_BROWSER_ITEM") == 0 && drag->DataSize > 1)
            {
                std::string dragPath((const char *)drag->Data, drag->DataSize - 1);
                if (std::filesystem::path(dragPath).extension() == ".lua")
                {
                    ImVec2 p0 = ImGui::GetWindowPos();
                    ImVec2 p1(p0.x + ImGui::GetWindowSize().x, p0.y + ImGui::GetWindowSize().y);
                    ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(80, 190, 80, 220), 4.f, 0, 2.f);
                }
            }

            ImRect winRect(ImGui::GetWindowPos(),
                           ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            if (ImGui::BeginDragDropTargetCustom(winRect, ImGui::GetID("##script_drop")))
            {
                if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string path((const char *)p->Data, p->DataSize - 1);
                    if (std::filesystem::path(path).extension() == ".lua")
                        scene.SetNodeScript(scriptDropNode, path);
                }
                ImGui::EndDragDropTarget();
            }
        }

#ifdef PE_AUDIO
        // Whole-window audio file drop target — drop .wav/.mp3/.flac/.ogg onto
        // any selected node to create or update an audio source component
        if (audioDropNode)
        {
            static const char *audioExts[] = {".wav", ".mp3", ".flac", ".ogg"};
            auto isAudioFile = [&](const std::string &p)
            {
                std::string ext = std::filesystem::path(p).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                for (auto *e : audioExts)
                    if (ext == e)
                        return true;
                return false;
            };

            const ImGuiPayload *drag = ImGui::GetDragDropPayload();
            if (drag && strcmp(drag->DataType, "CONTENT_BROWSER_ITEM") == 0 && drag->DataSize > 1)
            {
                std::string dragPath((const char *)drag->Data, drag->DataSize - 1);
                if (isAudioFile(dragPath))
                {
                    ImVec2 p0 = ImGui::GetWindowPos();
                    ImVec2 p1(p0.x + ImGui::GetWindowSize().x, p0.y + ImGui::GetWindowSize().y);
                    ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(80, 140, 230, 220), 4.f, 0, 2.f);
                }
            }

            ImRect winRect(ImGui::GetWindowPos(),
                           ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            if (ImGui::BeginDragDropTargetCustom(winRect, ImGui::GetID("##audio_drop")))
            {
                if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string path((const char *)p->Data, p->DataSize - 1);
                    if (isAudioFile(path))
                    {
                        if (auto *as = GetGlobalSystem<AudioSystem>())
                        {
                            if (!as->HasSource(audioDropNode))
                            {
                                AudioSourceDesc desc;
                                desc.filePath = path;
                                as->AddSource(scene, audioDropNode, desc);
                            }
                            else if (auto *desc = as->GetSourceDesc(audioDropNode))
                            {
                                desc->filePath = path;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
#endif

        ImGui::End();
    }
} // namespace pe
