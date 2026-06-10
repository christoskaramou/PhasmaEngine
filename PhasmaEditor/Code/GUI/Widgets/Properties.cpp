#include "Properties.h"
#include "CameraWidget.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "GUI/RuntimeUiAuthoring.h"
#include "GUI/SpriteAuthoring.h"
#include "GUI/SkinnedStrip2DEditor.h"
#include "GUI/UndoRedo.h"
#include "LightWidget.h"
#include "MeshWidget.h"
#include "Particles.h"
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

        bool DrawCenteredIconButton(const char *id, const char *icon, const ImVec2 &size)
        {
            const bool clicked = ImGui::InvisibleButton(id, size);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImGuiStyle &style = ImGui::GetStyle();

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const ImU32 buttonColor = ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                                                         : hovered ? ImGuiCol_ButtonHovered
                                                                   : ImGuiCol_Button);
            drawList->AddRectFilled(min, max, buttonColor, style.FrameRounding);

            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            const ImVec2 iconPos(min.x + (size.x - iconSize.x) * 0.5f,
                                 min.y + (size.y - iconSize.y) * 0.5f - 1.0f);
            drawList->AddText(iconPos, ImGui::GetColorU32(ImGuiCol_Text), icon);
            return clicked;
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

        if (!ImGui::Begin("Properties", &m_open))
        {
            ImGui::End();
            return;
        }

        auto &sel = SelectionManager::Instance();
        if (!sel.HasSelection())
        {
            ImGui::TextDisabled("No object selected");
            ImGui::End();
            return;
        }

        Scene &scene = *GetActiveScene();
        if ((sel.GetSelectionType() == SelectionType::Node || sel.GetSelectionType() == SelectionType::Mesh) &&
            !scene.IsNodeAlive(sel.GetSelectedNode()))
        {
            sel.ClearSelection();
            ImGui::TextDisabled("No object selected");
            ImGui::End();
            return;
        }

        auto drawTransform = [&]()
        {
            if (auto *w = m_gui->GetWidget<TransformWidget>())
                w->DrawEmbed(sel.GetSelectedNode());
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

            if (refs.size() == 1)
            {
                if (ImGui::SmallButton("Remove Mesh Component"))
                    scene.SetMeshRef(node, -1);
                ui::ItemTooltip("Remove the mesh component from this node.");
            }

            if (ImGui::SmallButton("+ Add Mesh"))
                ImGui::OpenPopup("AddMeshPopup");
            ui::ItemTooltip("Attach an additional primitive mesh to this node.");

            if (ImGui::BeginPopup("AddMeshPopup"))
            {
                if (ImGui::MenuItem("Plane"))
                    attachPrimitive(node, Primitives::CreatePlane());
                ui::ItemTooltip("Attach a flat plane primitive.");
                if (ImGui::MenuItem("Grid"))
                    attachPrimitive(node, Primitives::CreateGrid());
                ui::ItemTooltip("Attach a subdivided grid primitive.");
                if (ImGui::MenuItem("Cube"))
                    attachPrimitive(node, Primitives::CreateCube());
                ui::ItemTooltip("Attach a cube primitive.");
                if (ImGui::MenuItem("Sphere"))
                    attachPrimitive(node, Primitives::CreateSphere());
                ui::ItemTooltip("Attach a sphere primitive.");
                if (ImGui::MenuItem("UV Sphere"))
                    attachPrimitive(node, Primitives::CreateUvSphere());
                ui::ItemTooltip("Attach a UV sphere primitive.");
                if (ImGui::MenuItem("Ico Sphere"))
                    attachPrimitive(node, Primitives::CreateIcoSphere());
                ui::ItemTooltip("Attach an ico-sphere primitive.");
                if (ImGui::MenuItem("Cylinder"))
                    attachPrimitive(node, Primitives::CreateCylinder());
                ui::ItemTooltip("Attach a cylinder primitive.");
                if (ImGui::MenuItem("Cone"))
                    attachPrimitive(node, Primitives::CreateCone());
                ui::ItemTooltip("Attach a cone primitive.");
                if (ImGui::MenuItem("Pyramid"))
                    attachPrimitive(node, Primitives::CreatePyramid());
                ui::ItemTooltip("Attach a pyramid primitive.");
                if (ImGui::MenuItem("Torus"))
                    attachPrimitive(node, Primitives::CreateTorus());
                ui::ItemTooltip("Attach a torus primitive.");
                if (ImGui::MenuItem("Circle"))
                    attachPrimitive(node, Primitives::CreateCircle());
                ui::ItemTooltip("Attach a circle primitive.");
                if (ImGui::MenuItem("Skinned Strip 2D"))
                    attachPrimitive(node, Primitives::CreateSkinnedStrip2D());
                ui::ItemTooltip("Attach a GPU-skinned 2D strip primitive.");
                if (ImGui::MenuItem("Quad"))
                    attachPrimitive(node, Primitives::CreateQuad());
                ui::ItemTooltip("Attach a quad primitive.");
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
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##Script"))
                scene.SetNodeScript(node, "");
            ui::ItemTooltip("Detach the Lua script from this node.");

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

            const int index = sel.GetSelectedCameraIndex();
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

            w->DrawEmbed(&scene, sel.GetSelectedLightType(), sel.GetSelectedLightIndex());
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

            w->DrawEmbed(pm, sel.GetSelectedEmitterIndex());
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
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove Runtime UI"))
                {
                    scene.RemoveComponentFlag(node, Component_RuntimeUi);
                    if (m_gui)
                        m_gui->NotifyChange();
                }
                ui::ItemTooltip("Remove the Runtime UI tag from this node.");
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

            if (ImGui::DragFloat("Font Scale", &uiComponent->fontScale, 0.01f, 0.1f, 4.0f, "%.2f"))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("Visible", &uiComponent->visible))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("Draggable", &uiComponent->draggable))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("No Input", &uiComponent->noInput))
                markRuntimeUiChanged();
            if (ImGui::Checkbox("Bring To Front", &uiComponent->bringToFront))
                markRuntimeUiChanged();

            ImGui::Spacing();
            if (ImGui::SmallButton("Remove Runtime UI"))
            {
                scene.RemoveComponentFlag(node, Component_RuntimeUi);
                if (m_gui)
                    m_gui->NotifyChange();
            }
            ui::ItemTooltip("Remove the Runtime UI tag from this node.");
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

            if (ImGui::SmallButton("Remove Sprite Component"))
            {
                scene.ClearSpriteComponent(node);
                if (m_gui)
                    m_gui->NotifyChange();
            }
            ui::ItemTooltip("Remove the sprite authoring tag. The mesh and material remain unchanged.");
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

        auto drawAddComponentButton = [&](NodeId *node)
        {
            uint32_t flags = scene.GetComponentFlags(node);

            ImGui::Dummy(ImVec2(0.f, 4.f));
            ImGui::Separator();

            float btnWidth = ImGui::GetContentRegionAvail().x * 0.6f;
            float offset = (ImGui::GetContentRegionAvail().x - btnWidth) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

            if (ImGui::Button("+ Add Component", ImVec2(btnWidth, 0.f)))
                ImGui::OpenPopup("AddComponentPopup");
            ui::ItemTooltip("Open the menu of components that can be added to this node.");

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                if (!(flags & Component_Mesh))
                {
                    const bool meshMenuOpen = ImGui::BeginMenu("Mesh");
                    ui::ItemTooltip("Add a mesh component using a built-in primitive.");
                    if (meshMenuOpen)
                    {
                        if (ImGui::MenuItem("Plane"))
                            attachPrimitive(node, Primitives::CreatePlane());
                        ui::ItemTooltip("Add a flat plane mesh.");
                        if (ImGui::MenuItem("Grid"))
                            attachPrimitive(node, Primitives::CreateGrid());
                        ui::ItemTooltip("Add a subdivided grid mesh.");
                        if (ImGui::MenuItem("Cube"))
                            attachPrimitive(node, Primitives::CreateCube());
                        ui::ItemTooltip("Add a cube mesh.");
                        if (ImGui::MenuItem("Sphere"))
                            attachPrimitive(node, Primitives::CreateSphere());
                        ui::ItemTooltip("Add a sphere mesh.");
                        if (ImGui::MenuItem("UV Sphere"))
                            attachPrimitive(node, Primitives::CreateUvSphere());
                        ui::ItemTooltip("Add a UV sphere mesh.");
                        if (ImGui::MenuItem("Ico Sphere"))
                            attachPrimitive(node, Primitives::CreateIcoSphere());
                        ui::ItemTooltip("Add an ico-sphere mesh.");
                        if (ImGui::MenuItem("Cylinder"))
                            attachPrimitive(node, Primitives::CreateCylinder());
                        ui::ItemTooltip("Add a cylinder mesh.");
                        if (ImGui::MenuItem("Cone"))
                            attachPrimitive(node, Primitives::CreateCone());
                        ui::ItemTooltip("Add a cone mesh.");
                        if (ImGui::MenuItem("Pyramid"))
                            attachPrimitive(node, Primitives::CreatePyramid());
                        ui::ItemTooltip("Add a pyramid mesh.");
                        if (ImGui::MenuItem("Torus"))
                            attachPrimitive(node, Primitives::CreateTorus());
                        ui::ItemTooltip("Add a torus mesh.");
                        if (ImGui::MenuItem("Circle"))
                            attachPrimitive(node, Primitives::CreateCircle());
                        ui::ItemTooltip("Add a circle mesh.");
                        if (ImGui::MenuItem("Skinned Strip 2D"))
                            attachPrimitive(node, Primitives::CreateSkinnedStrip2D());
                        ui::ItemTooltip("Add a GPU-skinned 2D strip mesh.");
                        if (ImGui::MenuItem("Quad"))
                            attachPrimitive(node, Primitives::CreateQuad());
                        ui::ItemTooltip("Add a quad mesh.");
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
                if (DrawCenteredIconButton("##Browse", ICON_FA_FOLDER, ImVec2(buttonSize, buttonSize)))
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
                if (DrawCenteredIconButton("##Default", ICON_FA_ROTATE_LEFT, ImVec2(buttonSize, buttonSize)))
                    applyPath(GlobalSettings::DefaultSkyboxPath);
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

        switch (sel.GetSelectionType())
        {
        case SelectionType::Node:
        {
            NodeId *node = sel.GetSelectedNode();
            if (!node)
                break;

            uint32_t flags = scene.GetComponentFlags(node);
            if (!(flags & (Component_Camera | Component_Light | Component_Skybox)))
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

            // Transform is always shown for scene objects with spatial meaning
            drawTransform();

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

            // Script component
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

            // Camera component
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

            // Light component
            if (flags & Component_Light)
            {
                ImGui::Separator();
                auto [lt, idx] = scene.GetLightForNode(node);
                if (idx >= 0)
                {
                    if (auto *w = m_gui->GetWidget<LightWidget>())
                        w->DrawEmbed(&scene, lt, idx);
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

            if (!(flags & (Component_Camera | Component_Light | Component_Skybox)))
                drawAddComponentButton(node);
            break;
        }
        case SelectionType::Mesh:
        {
            // Kept for backward compatibility — mesh selection can still come from code paths
            NodeId *node = sel.GetSelectedNode();
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
