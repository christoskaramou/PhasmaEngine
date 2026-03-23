#include "Hierarchy.h"
#include "Camera/Camera.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/UndoRedo.h"
#include "GUI/IconsFontAwesome.h"
#include "Particles/ParticleManager.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui.h"

namespace pe
{
    // Unity-style colors
    namespace HierarchyStyle
    {
        // Unity dark theme colors
        const ImVec4 WindowBg = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
        const ImVec4 HeaderBg = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        const ImVec4 HeaderHovered = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);
        const ImVec4 HeaderActive = ImVec4(0.26f, 0.59f, 0.98f, 0.8f);
        const ImVec4 SelectionBg = ImVec4(0.17f, 0.36f, 0.53f, 1.0f);
        const ImVec4 SelectionBgUnfocused = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        const ImVec4 TextNormal = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
        const ImVec4 TextDisabled = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
        const ImVec4 TreeLineBg = ImVec4(0.35f, 0.35f, 0.35f, 0.5f);
    } // namespace HierarchyStyle

    struct HierarchyDragDropPayload
    {
        NodeId *node;
    };

    Hierarchy::Hierarchy() : Widget("Hierarchy")
    {
    }

    void Hierarchy::Update()
    {
        if (!m_open)
            return;

        bool useUnityStyle = GUIState::s_guiStyle == GUIStyle::Unity;
        int styleColorCount = 0;
        int styleVarCount = 0;

        // Apply Unity-style overrides only when Unity style is selected
        if (useUnityStyle)
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, HierarchyStyle::WindowBg);
            ImGui::PushStyleColor(ImGuiCol_Header, HierarchyStyle::SelectionBg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, HierarchyStyle::HeaderHovered);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, HierarchyStyle::HeaderActive);
            ImGui::PushStyleColor(ImGuiCol_Text, HierarchyStyle::TextNormal);
            styleColorCount = 5;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            styleVarCount = 3;
        }

        ImGui::Begin("Hierarchy", &m_open);

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        auto &selection = SelectionManager::Instance();

        // Helper: save undo snapshot before any destructive action
        auto recordUndo = [&scene]()
        { UndoRedo::Instance().RecordSnapshot(scene); };

        // Add Button
        float buttonWidth = ImGui::GetContentRegionAvail().x * 0.8f;
        float x = (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        if (ImGui::Button("Add", ImVec2(buttonWidth, 0.f)))
            ImGui::OpenPopup("AddEntityPopup");

        // Scene name with new scene button
        {
            std::string sceneName = scene.GetSceneName();
            if (scene.IsDirty())
                sceneName += " *";

            std::string label = std::string(ICON_FA_SITEMAP) + "  " + sceneName;
            float btnSize = ImGui::GetFrameHeight();
            float textWidth = ImGui::CalcTextSize(label.c_str()).x;
            float availWidth = ImGui::GetContentRegionAvail().x;
            float totalWidth = btnSize + ImGui::GetStyle().ItemSpacing.x + textWidth;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - totalWidth) * 0.5f);

            if (ImGui::Button(ICON_FA_PLUS, ImVec2(btnSize, btnSize)))
                m_gui->NewScene();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("New Scene");

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.8f, 1.0f));
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        if (ImGui::BeginPopup("AddEntityPopup"))
        {
            if (ImGui::MenuItem("Camera"))
            {
                scene.AddCamera();
            }

            if (ImGui::BeginMenu("Light"))
            {
                LightSystem *lightSystem = GetGlobalSystem<LightSystem>();

                if (ImGui::MenuItem("Directional Light"))
                    lightSystem->CreateDirectionalLight();
                if (ImGui::MenuItem("Point Light"))
                    lightSystem->CreatePointLight();
                if (ImGui::MenuItem("Spot Light"))
                    lightSystem->CreateSpotLight();
                if (ImGui::MenuItem("Area Light"))
                    lightSystem->CreateAreaLight();
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Empty Node"))
            {
                NodeId *node = scene.CreateNode("Empty Node");
                selection.Select(node, SelectionType::Node);
            }

            if (ImGui::BeginMenu("Mesh"))
            {
                auto AddPrimitive = [&](ModelAsset *m)
                {
                    EventSystem::PushEvent(EventType::ModelLoaded, m);
                    // After model is added to scene, select its first root node
                    // The event handler will call AddModel which creates NodeIds
                };
                if (ImGui::MenuItem("Plane"))
                    AddPrimitive(Primitives::CreatePlane());
                if (ImGui::MenuItem("Cube"))
                    AddPrimitive(Primitives::CreateCube());
                if (ImGui::MenuItem("Sphere"))
                    AddPrimitive(Primitives::CreateSphere());
                if (ImGui::MenuItem("Cylinder"))
                    AddPrimitive(Primitives::CreateCylinder());
                if (ImGui::MenuItem("Cone"))
                    AddPrimitive(Primitives::CreateCone());
                if (ImGui::MenuItem("Quad"))
                    AddPrimitive(Primitives::CreateQuad());
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Particle Emitter"))
            {
                ParticleManager *pm = scene.GetParticleManager();
                if (pm)
                {
                    auto &emitters = pm->GetEmitters();
                    auto &names = pm->GetEmitterNames();

                    Camera *camera = scene.GetActiveCamera();
                    vec3 spawnPos = camera ? (camera->GetPosition() + camera->GetFront() * 5.0f) : vec3(0.0f);

                    ParticleEmitter newEmitter{};
                    newEmitter.position = vec4(spawnPos, 1.0f);
                    newEmitter.velocity = vec4(0.0f, 5.0f, 0.0f, 0.0f);
                    newEmitter.colorStart = vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    newEmitter.colorEnd = vec4(0.0f, 0.0f, 0.0f, 0.0f);
                    newEmitter.sizeLife = vec4(0.05f, 0.15f, 1.0f, 2.0f);
                    newEmitter.physics = vec4(50.0f, 0.5f, 1.0f, 0.1f);
                    newEmitter.gravity = vec4(0.0f, -9.8f, 0.0f, 0.0f);
                    newEmitter.animation = vec4(1.0f, 1.0f, 1.0f, 0.0f);
                    newEmitter.textureIndex = 0;
                    newEmitter.count = 100;

                    emitters.push_back(newEmitter);
                    names.push_back("Emitter " + std::to_string(emitters.size() - 1));
                    pm->UpdateEmitterBuffer();

                    selection.SelectEmitter(static_cast<int>(emitters.size() - 1));
                }
            }

            ImGui::EndPopup();
        }

        static NodeId *s_renameNode = nullptr;
        static Camera *s_renameCamera = nullptr;
        static LightType s_renameLightType = (LightType)-1;
        static int s_renameLightIndex = -1;
        static int s_renameEmitterIndex = -1;
        static char s_renameBuf[128] = "";
        static bool s_openRenamePopup = false;
        std::vector<NodeId *> nodesToDelete;

        // Check for selection change (auto-expand)
        NodeId *currentSelectedNode = selection.GetSelectedNode();
        if (currentSelectedNode != m_lastSelectedNode)
        {
            m_lastSelectedNode = currentSelectedNode;

            if (currentSelectedNode)
            {
                m_nodeToExpand = currentSelectedNode;
                m_nodesToExpand.clear();
                m_scrollToSelection = true;

                // Trace parents up to root
                NodeId *p = scene.GetParent(currentSelectedNode);
                while (p)
                {
                    m_nodesToExpand.insert(p);
                    p = scene.GetParent(p);
                }
            }
        }

        // Cameras listing
        auto &cameras = scene.GetCameras();
        for (int i = 0; i < (int)cameras.size(); i++)
        {
            Camera *camera = cameras[i];
            ImGuiTreeNodeFlags cameraFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                             ImGuiTreeNodeFlags_Leaf |
                                             ImGuiTreeNodeFlags_FramePadding;

            if (selection.GetSelectionType() == SelectionType::Camera && selection.GetSelectedCameraIndex() == i)
                cameraFlags |= ImGuiTreeNodeFlags_Selected;

            std::string cameraLabel = camera->GetName();
            if (i == 0)
                cameraLabel += " (Main)";

            bool cameraOpen = ImGui::TreeNodeEx((void *)camera, cameraFlags, "%s  %s", ICON_FA_VIDEO, cameraLabel.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                selection.SelectCamera(i);
                ImGui::SetWindowFocus("Properties");
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (i > 0 && ImGui::MenuItem("Make Main"))
                {
                    scene.SetActiveCamera(camera);
                }
                if (i > 0 && ImGui::MenuItem("Focus"))
                {
                    selection.SelectCamera(i);
                    Camera *activeCamera = scene.GetActiveCamera();
                    vec3 center = camera->GetPosition();
                    vec3 dir = activeCamera->GetFront();
                    activeCamera->SetPosition(center - dir * 2.0f);
                    ImGui::SetWindowFocus("Properties");
                }
                if (ImGui::MenuItem("Rename"))
                {
                    s_renameNode = nullptr;
                    s_renameCamera = camera;
                    s_renameEmitterIndex = -1;
                    snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", camera->GetName().c_str());
                    s_openRenamePopup = true;
                }
                if (cameras.size() > 1 && ImGui::MenuItem("Delete"))
                {
                    recordUndo();
                    scene.RemoveCamera(camera);
                }
                ImGui::EndPopup();
            }

            if (cameraOpen)
            {
                ImGui::TreePop();
            }
        }

        // Lights
        {
            LightSystem *lightSystem = GetGlobalSystem<LightSystem>();
            bool hasLights = lightSystem &&
                             (!lightSystem->GetDirectionalLights().empty() ||
                              !lightSystem->GetPointLights().empty() ||
                              !lightSystem->GetSpotLights().empty() ||
                              !lightSystem->GetAreaLights().empty());

            if (hasLights && ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding))
            {
                if (ImGui::BeginPopupContextItem("LightsContextMenu"))
                {
                    if (ImGui::MenuItem("Delete All Lights"))
                    {
                        recordUndo();
                        lightSystem->GetDirectionalLights().clear();
                        lightSystem->GetPointLights().clear();
                        lightSystem->GetSpotLights().clear();
                        lightSystem->GetAreaLights().clear();
                        if (selection.GetSelectionType() == SelectionType::Light)
                            selection.ClearSelection();
                    }
                    ImGui::EndPopup();
                }

                ImGuiTreeNodeFlags lightFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_FramePadding;

                // Directional Lights
                for (int i = 0; i < (int)lightSystem->GetDirectionalLights().size(); i++)
                {
                    if (selection.GetSelectionType() == SelectionType::Light &&
                        selection.GetSelectedLightType() == LightType::Directional &&
                        selection.GetSelectedLightIndex() == i)
                        lightFlags |= ImGuiTreeNodeFlags_Selected;
                    else
                        lightFlags &= ~ImGuiTreeNodeFlags_Selected;

                    std::string name = lightSystem->GetDirectionalLights()[i].name;
                    ImGui::TreeNodeEx((void *)(intptr_t)(i + 4000), lightFlags, "%s  %s", ICON_FA_SUN, name.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        selection.Select(LightType::Directional, i);
                        ImGui::SetWindowFocus("Properties");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        Camera *camera = scene.GetActiveCamera();
                        DirectionalLight &light = lightSystem->GetDirectionalLights()[i];
                        vec3 pos = vec3(light.position);
                        camera->SetPosition(pos - camera->GetFront() * 10.0f);
                    }
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Focus"))
                        {
                            selection.Select(LightType::Directional, i);
                            Camera *camera = scene.GetActiveCamera();
                            DirectionalLight &light = lightSystem->GetDirectionalLights()[i];
                            vec3 pos = vec3(light.position);
                            camera->SetPosition(pos - camera->GetFront() * 10.0f);
                            ImGui::SetWindowFocus("Properties");
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            s_renameNode = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Directional;
                            s_renameLightIndex = i;
                            s_renameEmitterIndex = -1;
                            snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", name.c_str());
                            s_openRenamePopup = true;
                        }
                        if (ImGui::MenuItem("Delete"))
                        {
                            recordUndo();
                            auto &lights = lightSystem->GetDirectionalLights();
                            lights.erase(lights.begin() + i);
                            selection.ClearSelection();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TreePop();
                }

                // Point Lights
                for (int i = 0; i < (int)lightSystem->GetPointLights().size(); i++)
                {
                    if (selection.GetSelectionType() == SelectionType::Light &&
                        selection.GetSelectedLightType() == LightType::Point &&
                        selection.GetSelectedLightIndex() == i)
                        lightFlags |= ImGuiTreeNodeFlags_Selected;
                    else
                        lightFlags &= ~ImGuiTreeNodeFlags_Selected;

                    std::string name = lightSystem->GetPointLights()[i].name;
                    ImGui::TreeNodeEx((void *)(intptr_t)(i + 1000), lightFlags, "%s  %s", ICON_FA_LIGHTBULB, name.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        selection.Select(LightType::Point, i);
                        ImGui::SetWindowFocus("Properties");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        Camera *camera = scene.GetActiveCamera();
                        vec3 pos = vec3(lightSystem->GetPointLights()[i].position);
                        camera->SetPosition(pos - camera->GetFront() * 5.0f);
                    }
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Focus"))
                        {
                            selection.Select(LightType::Point, i);
                            Camera *camera = scene.GetActiveCamera();
                            vec3 pos = vec3(lightSystem->GetPointLights()[i].position);
                            camera->SetPosition(pos - camera->GetFront() * 5.0f);
                            ImGui::SetWindowFocus("Properties");
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            s_renameNode = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Point;
                            s_renameLightIndex = i;
                            s_renameEmitterIndex = -1;
                            snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", name.c_str());
                            s_openRenamePopup = true;
                        }
                        if (ImGui::MenuItem("Delete"))
                        {
                            recordUndo();
                            auto &lights = lightSystem->GetPointLights();
                            lights.erase(lights.begin() + i);
                            selection.ClearSelection();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TreePop();
                }

                // Spot Lights
                for (int i = 0; i < (int)lightSystem->GetSpotLights().size(); i++)
                {
                    if (selection.GetSelectionType() == SelectionType::Light &&
                        selection.GetSelectedLightType() == LightType::Spot &&
                        selection.GetSelectedLightIndex() == i)
                        lightFlags |= ImGuiTreeNodeFlags_Selected;
                    else
                        lightFlags &= ~ImGuiTreeNodeFlags_Selected;

                    std::string name = lightSystem->GetSpotLights()[i].name;
                    ImGui::TreeNodeEx((void *)(intptr_t)(i + 2000), lightFlags, "%s  %s", ICON_FA_LIGHTBULB, name.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        selection.Select(LightType::Spot, i);
                        ImGui::SetWindowFocus("Properties");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        Camera *camera = scene.GetActiveCamera();
                        vec3 pos = lightSystem->GetSpotLights()[i].position;
                        camera->SetPosition(pos - camera->GetFront() * 5.0f);
                    }
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Focus"))
                        {
                            selection.Select(LightType::Spot, i);
                            Camera *camera = scene.GetActiveCamera();
                            vec3 pos = lightSystem->GetSpotLights()[i].position;
                            camera->SetPosition(pos - camera->GetFront() * 5.0f);
                            ImGui::SetWindowFocus("Properties");
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            s_renameNode = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Spot;
                            s_renameLightIndex = i;
                            s_renameEmitterIndex = -1;
                            snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", name.c_str());
                            s_openRenamePopup = true;
                        }
                        if (ImGui::MenuItem("Delete"))
                        {
                            recordUndo();
                            auto &lights = lightSystem->GetSpotLights();
                            lights.erase(lights.begin() + i);
                            selection.ClearSelection();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TreePop();
                }

                // Area Lights
                for (int i = 0; i < (int)lightSystem->GetAreaLights().size(); i++)
                {
                    if (selection.GetSelectionType() == SelectionType::Light &&
                        selection.GetSelectedLightType() == LightType::Area &&
                        selection.GetSelectedLightIndex() == i)
                        lightFlags |= ImGuiTreeNodeFlags_Selected;
                    else
                        lightFlags &= ~ImGuiTreeNodeFlags_Selected;

                    std::string name = lightSystem->GetAreaLights()[i].name;
                    ImGui::TreeNodeEx((void *)(intptr_t)(i + 3000), lightFlags, "%s  %s", ICON_FA_LIGHTBULB, name.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        selection.Select(LightType::Area, i);
                        ImGui::SetWindowFocus("Properties");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        Camera *camera = scene.GetActiveCamera();
                        vec3 pos = lightSystem->GetAreaLights()[i].position;
                        camera->SetPosition(pos - camera->GetFront() * 5.0f);
                    }
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Focus"))
                        {
                            selection.Select(LightType::Area, i);
                            Camera *camera = scene.GetActiveCamera();
                            vec3 pos = lightSystem->GetAreaLights()[i].position;
                            camera->SetPosition(pos - camera->GetFront() * 5.0f);
                            ImGui::SetWindowFocus("Properties");
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            s_renameNode = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Area;
                            s_renameLightIndex = i;
                            s_renameEmitterIndex = -1;
                            snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", name.c_str());
                            s_openRenamePopup = true;
                        }
                        if (ImGui::MenuItem("Delete"))
                        {
                            recordUndo();
                            auto &lights = lightSystem->GetAreaLights();
                            lights.erase(lights.begin() + i);
                            selection.ClearSelection();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }
        }

        // Particle Emitters
        {
            ParticleManager *pm = scene.GetParticleManager();
            if (pm)
            {
                auto &emitters = pm->GetEmitters();
                auto &names = pm->GetEmitterNames();

                // Ensure names vector matches emitters size
                while (names.size() < emitters.size())
                    names.push_back("Emitter " + std::to_string(names.size()));

                if (!emitters.empty() && ImGui::TreeNodeEx("Particle Emitters", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding))
                {
                    bool deleteAllEmitters = false;
                    if (ImGui::BeginPopupContextItem("EmittersContextMenu"))
                    {
                        if (ImGui::MenuItem("Delete All Emitters"))
                        {
                            recordUndo();
                            deleteAllEmitters = true;
                        }
                        ImGui::EndPopup();
                    }

                    int emitterToDelete = -1;

                    if (!deleteAllEmitters)
                        for (int i = 0; i < static_cast<int>(emitters.size()); i++)
                        {
                            ImGuiTreeNodeFlags emitterFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                              ImGuiTreeNodeFlags_Leaf |
                                                              ImGuiTreeNodeFlags_FramePadding;

                            if (selection.GetSelectionType() == SelectionType::Emitter &&
                                selection.GetSelectedEmitterIndex() == i)
                                emitterFlags |= ImGuiTreeNodeFlags_Selected;

                            std::string emitterName = (i < static_cast<int>(names.size())) ? names[i] : ("Emitter " + std::to_string(i));

                            bool emitterOpen = ImGui::TreeNodeEx((void *)(intptr_t)(i + 5000), emitterFlags, "%s  %s", ICON_FA_FIRE, emitterName.c_str());

                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            {
                                selection.SelectEmitter(i);
                                ImGui::SetWindowFocus("Properties");
                            }

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                            {
                                Camera *camera = scene.GetActiveCamera();
                                vec3 emitterPos = vec3(emitters[i].position);
                                camera->SetPosition(emitterPos - camera->GetFront() * 5.0f);
                            }

                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Focus"))
                                {
                                    selection.SelectEmitter(i);
                                    Camera *camera = scene.GetActiveCamera();
                                    vec3 emitterPos = vec3(emitters[i].position);
                                    camera->SetPosition(emitterPos - camera->GetFront() * 5.0f);
                                    ImGui::SetWindowFocus("Properties");
                                }
                                if (ImGui::MenuItem("Rename"))
                                {
                                    s_renameNode = nullptr;
                                    s_renameCamera = nullptr;
                                    s_renameLightType = (LightType)-1;
                                    s_renameLightIndex = -1;
                                    s_renameEmitterIndex = i;
                                    snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", emitterName.c_str());
                                    s_openRenamePopup = true;
                                }
                                if (ImGui::MenuItem("Delete"))
                                {
                                    recordUndo();
                                    emitterToDelete = i;
                                }
                                ImGui::EndPopup();
                            }

                            if (emitterOpen)
                                ImGui::TreePop();
                        }

                    if (deleteAllEmitters)
                    {
                        emitters.clear();
                        names.clear();
                        pm->UpdateEmitterBuffer();
                        if (selection.GetSelectionType() == SelectionType::Emitter)
                            selection.ClearSelection();
                    }
                    else if (emitterToDelete >= 0)
                    {
                        emitters.erase(emitters.begin() + emitterToDelete);
                        if (emitterToDelete < static_cast<int>(names.size()))
                            names.erase(names.begin() + emitterToDelete);
                        pm->UpdateEmitterBuffer();
                        if (selection.GetSelectionType() == SelectionType::Emitter)
                            selection.ClearSelection();
                    }

                    ImGui::TreePop();
                }
            }
        }

        // --- Scene Nodes ---
        // Draw mesh entry under a node
        auto DrawMeshEntry = [&](NodeId *node, int meshIndex)
        {
            uintptr_t meshUniqueId = reinterpret_cast<uintptr_t>(node) ^ 0x10000;
            std::string meshDisplayName = std::string(ICON_FA_SHAPES) + "  Mesh";

            ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_Leaf |
                                           ImGuiTreeNodeFlags_FramePadding;

            if (selection.GetSelectedNode() == node && selection.GetSelectionType() == SelectionType::Mesh)
                meshFlags |= ImGuiTreeNodeFlags_Selected;

            bool meshOpen = ImGui::TreeNodeEx((void *)meshUniqueId, meshFlags, "%s", meshDisplayName.c_str());

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                selection.Select(node, SelectionType::Mesh);
                ImGui::SetWindowFocus("Properties");
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                Camera *camera = scene.GetActiveCamera();
                const AABB &bounds = scene.GetWorldAABB(node);
                vec3 center = (bounds.min + bounds.max) * 0.5f;
                float dist = glm::length(bounds.max - bounds.min);
                vec3 dir = camera->GetFront();
                camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Focus"))
                {
                    selection.Select(node, SelectionType::Mesh);
                    Camera *camera = scene.GetActiveCamera();
                    const AABB &bounds = scene.GetWorldAABB(node);
                    vec3 center = (bounds.min + bounds.max) * 0.5f;
                    float dist = glm::length(bounds.max - bounds.min);
                    vec3 dir = camera->GetFront();
                    camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                    ImGui::SetWindowFocus("Properties");
                }
                ImGui::EndPopup();
            }

            if (meshOpen)
                ImGui::TreePop();
        };

        // Recursive draw node
        auto DrawNode = [&](auto &&self, NodeId *node) -> void
        {
            const std::string &nodeName = scene.GetNodeName(node);
            auto &children = scene.GetChildren(node);
            bool hasChildren = !children.empty();
            int meshIndex = scene.GetMeshRef(node);
            bool hasMesh = meshIndex >= 0;

            // Auto-expand parents
            if (m_nodesToExpand.find(node) != m_nodesToExpand.end())
            {
                ImGui::SetNextItemOpen(true);
            }

            bool isLeaf = !hasChildren && !hasMesh;

            uintptr_t uniqueId = reinterpret_cast<uintptr_t>(node);

            // Choose icon
            const char *icon;
            std::string lowerName = nodeName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            if (lowerName.find("camera") != std::string::npos || lowerName.find("cam") != std::string::npos)
                icon = ICON_FA_VIDEO;
            else if (lowerName.find("light") != std::string::npos || lowerName.find("lamp") != std::string::npos)
                icon = ICON_FA_LIGHTBULB;
            else
                icon = ICON_FA_VECTOR_SQUARE;

            std::string displayNodeName = std::string(icon) + "  " + nodeName;

            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                           ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_FramePadding;
            if (isLeaf)
                nodeFlags |= ImGuiTreeNodeFlags_Leaf;

            // Highlight if selected
            if (selection.GetSelectedNode() == node && selection.GetSelectionType() == SelectionType::Node)
            {
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
                if (m_scrollToSelection && m_nodeToExpand == node)
                {
                    m_scrollToSelection = false;
                    m_nodeToExpand = nullptr;
                    m_nodesToExpand.clear();
                }
            }

            bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", displayNodeName.c_str());

            // Drag & Drop Source
            if (ImGui::BeginDragDropSource())
            {
                HierarchyDragDropPayload payload;
                payload.node = node;
                ImGui::SetDragDropPayload("HIERARCHY_NODE", &payload, sizeof(HierarchyDragDropPayload));
                ImGui::Text("%s", nodeName.c_str());
                ImGui::EndDragDropSource();
            }

            if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::IsItemToggledOpen())
            {
                selection.Select(node, SelectionType::Node);
                ImGui::SetWindowFocus("Properties");
            }

            // Drag & Drop Target
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
                {
                    HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                    scene.ReparentNode(data.node, node);
                }

                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    const char *pathStr = (const char *)payload->Data;
                    std::filesystem::path path(pathStr);

                    std::string ext = path.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    bool isModel = (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx");

                    if (isModel && !GUIState::s_modelLoading)
                    {
                        auto loadTask = [path]()
                        {
                            GUIState::s_modelLoading = true;
                            try
                            {
                                if (ModelAsset *m = ModelAsset::Load(path))
                                    EventSystem::PushEvent(EventType::ModelLoaded, m);
                            }
                            catch (const std::exception &e)
                            {
                                PE_WARN("[Scene] Failed to load model: %s", e.what());
                            }
                            GUIState::s_modelLoading = false;
                        };
                        ThreadPool::GUI.Enqueue(loadTask);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                Camera *camera = scene.GetActiveCamera();
                const AABB &bounds = scene.GetWorldAABB(node);
                vec3 center = (bounds.min + bounds.max) * 0.5f;
                float dist = glm::length(bounds.max - bounds.min);
                vec3 dir = camera->GetFront();
                camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Focus"))
                {
                    selection.Select(node, SelectionType::Node);
                    Camera *camera = scene.GetActiveCamera();
                    const AABB &bounds = scene.GetWorldAABB(node);
                    vec3 center = (bounds.min + bounds.max) * 0.5f;
                    float dist = glm::length(bounds.max - bounds.min);
                    vec3 dir = camera->GetFront();
                    camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                    ImGui::SetWindowFocus("Properties");
                }
                if (ImGui::MenuItem("Rename"))
                {
                    s_renameNode = node;
                    s_renameCamera = nullptr;
                    s_renameEmitterIndex = -1;
                    snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", nodeName.c_str());
                    s_openRenamePopup = true;
                }
                if (ImGui::BeginMenu("Add"))
                {
                    if (ImGui::MenuItem("Camera"))
                        scene.AddCamera();

                    if (ImGui::BeginMenu("Light"))
                    {
                        LightSystem *lightSystem = GetGlobalSystem<LightSystem>();
                        if (ImGui::MenuItem("Directional Light"))
                            lightSystem->CreateDirectionalLight();
                        if (ImGui::MenuItem("Point Light"))
                            lightSystem->CreatePointLight();
                        if (ImGui::MenuItem("Spot Light"))
                            lightSystem->CreateSpotLight();
                        if (ImGui::MenuItem("Area Light"))
                            lightSystem->CreateAreaLight();
                        ImGui::EndMenu();
                    }

                    if (ImGui::MenuItem("Empty Node"))
                    {
                        NodeId *newNode = scene.CreateNode("Empty Node", node);
                        scene.MarkNodeDirty(newNode);
                        selection.Select(newNode, SelectionType::Node);
                    }

                    if (ImGui::BeginMenu("Mesh"))
                    {
                        auto AddPrimitive = [&](ModelAsset *m)
                        {
                            EventSystem::PushEvent(EventType::ModelLoaded, m);
                        };
                        if (ImGui::MenuItem("Plane"))
                            AddPrimitive(Primitives::CreatePlane());
                        if (ImGui::MenuItem("Cube"))
                            AddPrimitive(Primitives::CreateCube());
                        if (ImGui::MenuItem("Sphere"))
                            AddPrimitive(Primitives::CreateSphere());
                        if (ImGui::MenuItem("Cylinder"))
                            AddPrimitive(Primitives::CreateCylinder());
                        if (ImGui::MenuItem("Cone"))
                            AddPrimitive(Primitives::CreateCone());
                        if (ImGui::MenuItem("Quad"))
                            AddPrimitive(Primitives::CreateQuad());
                        ImGui::EndMenu();
                    }

                    if (ImGui::MenuItem("Particle Emitter"))
                    {
                        ParticleManager *pm = scene.GetParticleManager();
                        if (pm)
                        {
                            auto &emitters = pm->GetEmitters();
                            auto &names = pm->GetEmitterNames();
                            Camera *activeCamera = scene.GetActiveCamera();
                            vec3 spawnPos = activeCamera ? (activeCamera->GetPosition() + activeCamera->GetFront() * 5.0f) : vec3(0.0f);

                            ParticleEmitter newEmitter{};
                            newEmitter.position = vec4(spawnPos, 1.0f);
                            newEmitter.velocity = vec4(0.0f, 5.0f, 0.0f, 0.0f);
                            newEmitter.colorStart = vec4(1.0f, 1.0f, 1.0f, 1.0f);
                            newEmitter.colorEnd = vec4(0.0f, 0.0f, 0.0f, 0.0f);
                            newEmitter.sizeLife = vec4(0.05f, 0.15f, 1.0f, 2.0f);
                            newEmitter.physics = vec4(50.0f, 0.5f, 1.0f, 0.1f);
                            newEmitter.gravity = vec4(0.0f, -9.8f, 0.0f, 0.0f);
                            newEmitter.animation = vec4(1.0f, 1.0f, 1.0f, 0.0f);
                            newEmitter.textureIndex = 0;
                            newEmitter.count = 100;

                            emitters.push_back(newEmitter);
                            names.push_back("Emitter " + std::to_string(emitters.size() - 1));
                            pm->UpdateEmitterBuffer();
                            selection.SelectEmitter(static_cast<int>(emitters.size() - 1));
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Delete"))
                {
                    recordUndo();
                    nodesToDelete.push_back(node);
                }
                ImGui::EndPopup();
            }

            if (nodeOpen)
            {
                if (hasMesh)
                    DrawMeshEntry(node, meshIndex);

                for (NodeId *child : children)
                    self(self, child);

                ImGui::TreePop();
            }
        };

        // Draw all root nodes (nodes without parents)
        uint32_t nodeCount = scene.GetNodeCount();
        for (uint32_t i = 0; i < nodeCount; i++)
        {
            NodeId *node = scene.GetNodeId(i);
            if (!scene.GetParent(node))
                DrawNode(DrawNode, node);
        }

        // Fill remaining space with dummy to allow dropping in empty area
        ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.y < 50.0f)
            available.y = 50.0f;
        ImGui::Dummy(available);

        // Window-wide Drop Target for loading models from FileBrowser
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char *pathStr = (const char *)payload->Data;
                std::filesystem::path path(pathStr);

                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                bool isModel = false;
                const char *modelExts[] = {".gltf", ".glb", ".obj", ".fbx"};
                for (const char *e : modelExts)
                {
                    if (ext == e)
                    {
                        isModel = true;
                        break;
                    }
                }

                if (isModel && !GUIState::s_modelLoading)
                {
                    auto loadTask = [path]()
                    {
                        GUIState::s_modelLoading = true;
                        try
                        {
                            if (ModelAsset *m = ModelAsset::Load(path))
                                EventSystem::PushEvent(EventType::ModelLoaded, m);
                        }
                        catch (const std::exception &e)
                        {
                            PE_WARN("[Scene] Failed to load model: %s", e.what());
                        }
                        GUIState::s_modelLoading = false;
                    };
                    ThreadPool::GUI.Enqueue(loadTask);
                }
            }

            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
            {
                HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                scene.ReparentNode(data.node, nullptr);
            }
            ImGui::EndDragDropTarget();
        }

        if (s_openRenamePopup)
        {
            ImGui::OpenPopup("Rename Entity");
            s_openRenamePopup = false;
        }

        if (ImGui::BeginPopupModal("Rename Entity", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", s_renameBuf, IM_ARRAYSIZE(s_renameBuf));
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                if (s_renameEmitterIndex >= 0)
                {
                    ParticleManager *pm = scene.GetParticleManager();
                    if (pm)
                    {
                        auto &emNames = pm->GetEmitterNames();
                        if (s_renameEmitterIndex < static_cast<int>(emNames.size()))
                            emNames[s_renameEmitterIndex] = s_renameBuf;
                    }
                }
                else if (s_renameNode)
                {
                    scene.SetNodeName(s_renameNode, s_renameBuf);
                }
                else if (s_renameCamera)
                {
                    s_renameCamera->SetName(s_renameBuf);
                }
                else if (s_renameLightIndex != -1)
                {
                    LightSystem *lightSystem = GetGlobalSystem<LightSystem>();
                    if (s_renameLightType == LightType::Directional)
                        lightSystem->GetDirectionalLights()[s_renameLightIndex].name = s_renameBuf;
                    else if (s_renameLightType == LightType::Point)
                        lightSystem->GetPointLights()[s_renameLightIndex].name = s_renameBuf;
                    else if (s_renameLightType == LightType::Spot)
                        lightSystem->GetSpotLights()[s_renameLightIndex].name = s_renameBuf;
                    else if (s_renameLightType == LightType::Area)
                        lightSystem->GetAreaLights()[s_renameLightIndex].name = s_renameBuf;
                }
                s_renameLightIndex = -1;
                s_renameEmitterIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Delete deferred nodes
        for (NodeId *node : nodesToDelete)
        {
            if (selection.GetSelectedNode() == node)
                selection.ClearSelection();
            scene.DeleteNode(node);
        }
        if (!nodesToDelete.empty())
            EventSystem::PushEvent(EventType::NodeRemoved);

        if (ImGui::BeginPopupContextWindow("HierarchyBgContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::BeginMenu("Add"))
            {
                if (ImGui::MenuItem("Camera"))
                    scene.AddCamera();

                if (ImGui::BeginMenu("Light"))
                {
                    LightSystem *ls = GetGlobalSystem<LightSystem>();
                    if (ImGui::MenuItem("Directional Light"))
                        ls->CreateDirectionalLight();
                    if (ImGui::MenuItem("Point Light"))
                        ls->CreatePointLight();
                    if (ImGui::MenuItem("Spot Light"))
                        ls->CreateSpotLight();
                    if (ImGui::MenuItem("Area Light"))
                        ls->CreateAreaLight();
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Empty Node"))
                {
                    NodeId *node = scene.CreateNode("Empty Node");
                    selection.Select(node, SelectionType::Node);
                }

                if (ImGui::BeginMenu("Mesh"))
                {
                    auto AddPrim = [&](ModelAsset *m)
                    { EventSystem::PushEvent(EventType::ModelLoaded, m); };
                    if (ImGui::MenuItem("Plane"))
                        AddPrim(Primitives::CreatePlane());
                    if (ImGui::MenuItem("Cube"))
                        AddPrim(Primitives::CreateCube());
                    if (ImGui::MenuItem("Sphere"))
                        AddPrim(Primitives::CreateSphere());
                    if (ImGui::MenuItem("Cylinder"))
                        AddPrim(Primitives::CreateCylinder());
                    if (ImGui::MenuItem("Cone"))
                        AddPrim(Primitives::CreateCone());
                    if (ImGui::MenuItem("Quad"))
                        AddPrim(Primitives::CreateQuad());
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::End();

        if (styleVarCount > 0)
            ImGui::PopStyleVar(styleVarCount);
        if (styleColorCount > 0)
            ImGui::PopStyleColor(styleColorCount);
    }
} // namespace pe
