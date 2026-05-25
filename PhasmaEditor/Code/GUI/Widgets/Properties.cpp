#include "Properties.h"
#include "CameraWidget.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "GUI/IconsFontAwesome.h"
#include "LightWidget.h"
#include "MeshWidget.h"
#include "Particles.h"
#include "Particles/ParticleManager.h"
#include "Render/SceneSky.h"
#include "Scene/NodeComponents.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Script/ScriptSystem.h"
#include "ScriptEditor.h"
#include "Systems/RendererSystem.h"
#include "TransformWidget.h"
#ifdef PE_PHYSICS
#include "PhysicsWidget.h"
#include "Physics/PhysicsTypes.h"
#include "Systems/PhysicsSystem.h"
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
                ImGui::SetTooltip("%s", displayPath);
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
                    if (open)
                    {
                        w->DrawEmbed(&mesh, node);
                        if (ImGui::SmallButton("Remove"))
                            removeIdx = meshIndex;
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
            }

            if (ImGui::SmallButton("+ Add Mesh"))
                ImGui::OpenPopup("AddMeshPopup");

            if (ImGui::BeginPopup("AddMeshPopup"))
            {
                if (ImGui::MenuItem("Plane"))
                    attachPrimitive(node, Primitives::CreatePlane());
                if (ImGui::MenuItem("Cube"))
                    attachPrimitive(node, Primitives::CreateCube());
                if (ImGui::MenuItem("Sphere"))
                    attachPrimitive(node, Primitives::CreateSphere());
                if (ImGui::MenuItem("Cylinder"))
                    attachPrimitive(node, Primitives::CreateCylinder());
                if (ImGui::MenuItem("Cone"))
                    attachPrimitive(node, Primitives::CreateCone());
                if (ImGui::MenuItem("Quad"))
                    attachPrimitive(node, Primitives::CreateQuad());
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
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##Script"))
                scene.SetNodeScript(node, "");

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

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                if (!(flags & Component_Mesh))
                {
                    if (ImGui::BeginMenu("Mesh"))
                    {
                        if (ImGui::MenuItem("Plane"))
                            attachPrimitive(node, Primitives::CreatePlane());
                        if (ImGui::MenuItem("Cube"))
                            attachPrimitive(node, Primitives::CreateCube());
                        if (ImGui::MenuItem("Sphere"))
                            attachPrimitive(node, Primitives::CreateSphere());
                        if (ImGui::MenuItem("Cylinder"))
                            attachPrimitive(node, Primitives::CreateCylinder());
                        if (ImGui::MenuItem("Cone"))
                            attachPrimitive(node, Primitives::CreateCone());
                        if (ImGui::MenuItem("Quad"))
                            attachPrimitive(node, Primitives::CreateQuad());
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
                }
#endif

                if (!(flags & Component_Script))
                {
                    if (ImGui::BeginMenu("Lua Script"))
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
                        if (ImGui::MenuItem("New Empty Script"))
                        {
                            if (auto *se = m_gui->GetWidget<ScriptEditor>())
                                se->OpenNewScript(node);
                        }
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

            auto applyPath = [&](bool daySky, std::string path)
            {
                std::string dayPath = skybox->dayPath;
                std::string nightPath = skybox->nightPath;
                if (daySky)
                    dayPath = std::move(path);
                else
                    nightPath = std::move(path);

                scene.SetSkyboxPaths(node, std::move(dayPath), std::move(nightPath));
                if (m_gui)
                    m_gui->NotifyChange();
            };

            auto drawRow = [&](const char *label, bool daySky)
            {
                const std::string &path = daySky ? skybox->dayPath : skybox->nightPath;
                ImGui::PushID(label);

                const ImGuiStyle &style = ImGui::GetStyle();
                const float buttonSize = ImGui::GetFrameHeight();
                const float clearWidth = ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f;
                const float labelWidth = ImGui::CalcTextSize("Night").x;
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
                ImGui::TextUnformatted(label);
                ImGui::SetCursorPos(ImVec2(rowStartX + labelWidth + style.ItemInnerSpacing.x, rowStartY));
                DrawSkyboxPathPreview(path, pathWidth);

                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (DrawCenteredIconButton("##Browse", ICON_FA_FOLDER, ImVec2(buttonSize, buttonSize)))
                {
                    if (auto *fs = m_gui ? m_gui->GetWidget<FileSelector>() : nullptr)
                    {
                        fs->OpenSelection([node, daySky, this](const std::string &selectedPath) -> bool
                                          {
                            Scene *activeScene = GetActiveScene();
                            if (!activeScene || !activeScene->IsNodeAlive(node))
                                return true;

                            NodeSkyboxTag *activeSkybox = activeScene->GetSkyboxForNode(node);
                            if (!activeSkybox)
                                return true;

                            std::string dayPath = activeSkybox->dayPath;
                            std::string nightPath = activeSkybox->nightPath;
                            if (daySky)
                                dayPath = MakeSceneSkyPathSetting(selectedPath);
                            else
                                nightPath = MakeSceneSkyPathSetting(selectedPath);

                            activeScene->SetSkyboxPaths(node, std::move(dayPath), std::move(nightPath));
                            if (m_gui)
                                m_gui->NotifyChange();
                            return true; },
                                          SkyboxExtensions(),
                                          Path::Assets + "Skyboxes");
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Browse");

                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (path.empty())
                    ImGui::BeginDisabled();
                if (ImGui::Button("Clear", ImVec2(clearWidth, buttonSize)))
                    applyPath(daySky, {});
                if (path.empty())
                    ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Clear");

                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (DrawCenteredIconButton("##Default", ICON_FA_ROTATE_LEFT, ImVec2(buttonSize, buttonSize)))
                    applyPath(daySky,
                              daySky ? GlobalSettings::DefaultSkyboxDayPath
                                     : GlobalSettings::DefaultSkyboxNightPath);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Default");
                ImGui::PopStyleVar();
                ImGui::PopID();
            };

            drawRow("Day", true);
            ImGui::Spacing();
            drawRow("Night", false);
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
                if (ImGui::CollapsingHeader("Skybox Component", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(8.f);
                    drawSkyboxComponent(node);
                    ImGui::Unindent(8.f);
                }
                break;
            }

            // Transform is always shown for scene objects with spatial meaning
            drawTransform();

            // Mesh component
            if (flags & Component_Mesh)
            {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen))
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
                if (open)
                    drawScriptComponent(node);
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
                if (ImGui::CollapsingHeader("Physics Component", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(8.f);
                    if (auto *w = m_gui->GetWidget<PhysicsWidget>())
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
                if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent(8.f);
                    if (auto *w = m_gui->GetWidget<AudioWidget>())
                        w->DrawEmbed(node, &scene);
                    ImGui::Unindent(8.f);
                }
            }
#endif

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
