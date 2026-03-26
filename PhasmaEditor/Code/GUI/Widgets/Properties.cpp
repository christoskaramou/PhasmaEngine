#include "Properties.h"
#include "CameraWidget.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "LightWidget.h"
#include "MeshWidget.h"
#include "Particles.h"
#include "Particles/ParticleManager.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "ScriptEditor.h"
#include "Systems/RendererSystem.h"
#include "TransformWidget.h"

namespace pe
{
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

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();

        auto drawTransform = [&]()
        {
            if (auto *w = m_gui->GetWidget<TransformWidget>())
                w->DrawEmbed(sel.GetSelectedNode());
        };

        auto drawMeshComponent = [&](NodeId *node)
        {
            auto *w = m_gui->GetWidget<MeshWidget>();
            if (!w)
                return;

            int meshIndex = scene.GetMeshRef(node);
            if (meshIndex < 0)
                return;

            Mesh &mesh = scene.GetMesh(meshIndex);
            w->DrawEmbed(&mesh, node);

            ImGui::Dummy(ImVec2(0.f, 2.f));
            if (ImGui::SmallButton("Remove Mesh Component"))
                scene.SetMeshRef(node, -1);
        };

        auto drawScriptComponent = [&](NodeId *node)
        {
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

        auto attachPrimitive = [&](NodeId *node, ModelAsset *model)
        {
            EventSystem::PushEvent(EventType::PrimitiveAttachedToNode, Scene::PrimitiveAttachRequest{node, model});
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

        switch (sel.GetSelectionType())
        {
        case SelectionType::Node:
        {
            NodeId *node = sel.GetSelectedNode();
            if (!node)
                break;

            uint32_t flags = scene.GetComponentFlags(node);

            // Transform is always shown
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
                if (ImGui::CollapsingHeader("Script Component", ImGuiTreeNodeFlags_DefaultOpen))
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

            if (!(flags & (Component_Camera | Component_Light)))
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

        ImGui::End();
    }
} // namespace pe
