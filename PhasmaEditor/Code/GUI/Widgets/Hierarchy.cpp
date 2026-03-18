#include "Hierarchy.h"
#include "Camera/Camera.h"
#include "GUI/GUIState.h"
#include "GUI/UndoRedo.h"
#include "GUI/IconsFontAwesome.h"
#include "Particles/ParticleManager.h"
#include "Scene/Model.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
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
        size_t modelId;
        int nodeIndex;
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
        auto &models = scene.GetModels();
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

        // Scene name
        {
            std::string sceneName = scene.GetSceneName();
            if (scene.IsDirty())
                sceneName += " *";

            std::string label = std::string(ICON_FA_SITEMAP) + "  " + sceneName;
            float textWidth = ImGui::CalcTextSize(label.c_str()).x;
            float availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
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
                Camera *camera = scene.AddCamera();
                // Select new camera
                // selection.Select(nullptr, -1, SelectionType::Camera); // Need logic to select specific camera index if multiples were supported in UI
            }

            if (ImGui::BeginMenu("Light"))
            {
                LightSystem *lightSystem = GetGlobalSystem<LightSystem>();

                if (ImGui::MenuItem("Directional Light"))
                {
                    lightSystem->CreateDirectionalLight();
                }
                if (ImGui::MenuItem("Point Light"))
                {
                    lightSystem->CreatePointLight();
                }
                if (ImGui::MenuItem("Spot Light"))
                {
                    lightSystem->CreateSpotLight();
                }
                if (ImGui::MenuItem("Area Light"))
                {
                    lightSystem->CreateAreaLight();
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Node"))
            {
                Model *model = new Model();
                NodeInfo nodeInfo{};
                nodeInfo.name = "Node";
                nodeInfo.localMatrix = mat4(1.0f);
                model->GetNodeInfos().push_back(nodeInfo);
                EventSystem::PushEvent(EventType::ModelLoaded, model);
                // Select the new node (index 0)
                selection.Select(model, 0, SelectionType::Node);
            }

            if (ImGui::BeginMenu("Mesh"))
            {
                if (ImGui::MenuItem("Plane"))
                {
                    Model *model = Primitives::CreatePlane();
                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                    selection.Select(model, 0, SelectionType::Node);
                }
                if (ImGui::MenuItem("Cube"))
                {
                    Model *model = Primitives::CreateCube();
                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                    selection.Select(model, 0, SelectionType::Node);
                }
                if (ImGui::MenuItem("Sphere"))
                {
                    Model *model = Primitives::CreateSphere();
                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                    selection.Select(model, 0, SelectionType::Node);
                }
                if (ImGui::MenuItem("Cylinder"))
                {
                    Model *model = Primitives::CreateCylinder();
                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                    selection.Select(model, 0, SelectionType::Node);
                }
                if (ImGui::MenuItem("Cone"))
                {
                    Model *model = Primitives::CreateCone();
                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                    selection.Select(model, 0, SelectionType::Node);
                }
                if (ImGui::MenuItem("Quad"))
                {
                    Model *model = Primitives::CreateQuad();
                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                    selection.Select(model, 0, SelectionType::Node);
                }
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

        static Model *s_renameModel = nullptr;
        static Camera *s_renameCamera = nullptr;
        static LightType s_renameLightType = (LightType)-1;
        static int s_renameLightIndex = -1;
        static int s_renameNode = -1;
        static char s_renameBuf[128] = "";
        static bool s_openRenamePopup = false;
        std::vector<Model *> modelsToDelete;

        // Check for selection change
        Model *currentSelectedModel = selection.GetSelectedModel();
        int currentSelectedNodeIndex = selection.GetSelectedNodeIndex();

        if (currentSelectedModel != m_lastSelectedModel || currentSelectedNodeIndex != m_lastSelectedNodeIndex)
        {
            m_lastSelectedModel = currentSelectedModel;
            m_lastSelectedNodeIndex = currentSelectedNodeIndex;

            if (currentSelectedModel && currentSelectedNodeIndex >= 0)
            {
                m_modelToExpand = currentSelectedModel;
                m_nodesToExpand.clear();
                m_scrollToSelection = true;

                // Trace parents up to root
                int p = currentSelectedModel->GetNodeInfos()[currentSelectedNodeIndex].parent;
                while (p >= 0)
                {
                    m_nodesToExpand.insert(p);
                    p = currentSelectedModel->GetNodeInfos()[p].parent;
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

            if (selection.GetSelectionType() == SelectionType::Camera && selection.GetSelectedNodeIndex() == i)
                cameraFlags |= ImGuiTreeNodeFlags_Selected;

            std::string cameraLabel = camera->GetName();
            if (i == 0)
                cameraLabel += " (Main)";

            bool cameraOpen = ImGui::TreeNodeEx((void *)camera, cameraFlags, "%s  %s", ICON_FA_VIDEO, cameraLabel.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                selection.Select(nullptr, i, SelectionType::Camera);
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
                    selection.Select(nullptr, i, SelectionType::Camera);
                    Camera *activeCamera = scene.GetActiveCamera();
                    vec3 center = camera->GetPosition();
                    vec3 dir = activeCamera->GetFront();
                    activeCamera->SetPosition(center - dir * 2.0f);
                    ImGui::SetWindowFocus("Properties");
                }
                if (ImGui::MenuItem("Rename"))
                {
                    s_renameModel = nullptr;
                    s_renameCamera = camera;
                    s_renameNode = -1;
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
                            s_renameModel = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Directional;
                            s_renameLightIndex = i;
                            s_renameNode = -1;
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
                            s_renameModel = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Point;
                            s_renameLightIndex = i;
                            s_renameNode = -1;
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
                            s_renameModel = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Spot;
                            s_renameLightIndex = i;
                            s_renameNode = -1;
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
                            s_renameModel = nullptr;
                            s_renameCamera = nullptr;
                            s_renameLightType = LightType::Area;
                            s_renameLightIndex = i;
                            s_renameNode = -1;
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
                                    s_renameModel = nullptr;
                                    s_renameCamera = nullptr;
                                    s_renameLightType = (LightType)-1;
                                    s_renameLightIndex = -1;
                                    s_renameNode = -(i + 100); // negative sentinel for emitter rename
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

        for (auto model : models)
        {
            if (!model)
                continue;

            size_t id = model->GetId();
            std::string name = model->GetLabel();
            if (name.empty())
                name = "Model_" + std::to_string(id);

            // Use node icon - the model entry acts as the root node
            std::string displayName = std::string(ICON_FA_VECTOR_SQUARE) + "  " + name;

            // Compute node hierarchy early (model entry merges with root nodes)
            const auto &nodes = model->GetNodeInfos();
            int nodeCount = static_cast<int>(nodes.size());
            std::vector<std::vector<int>> nodeChildren(nodeCount);
            std::vector<int> roots;
            for (int i = 0; i < nodeCount; ++i)
            {
                if (nodes[i].parent >= 0 && nodes[i].parent < nodeCount)
                    nodeChildren[nodes[i].parent].push_back(i);
                else
                    roots.push_back(i);
            }

            // Find the node this model entry represents (first root with a mesh, or first root)
            int modelNodeIdx = -1;
            for (int r : roots)
            {
                if (model->GetNodeMesh(r) >= 0)
                {
                    modelNodeIdx = r;
                    break;
                }
            }
            if (modelNodeIdx < 0 && !roots.empty())
                modelNodeIdx = roots[0];

            ImGuiTreeNodeFlags modelFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_OpenOnArrow |
                                            ImGuiTreeNodeFlags_DefaultOpen |
                                            ImGuiTreeNodeFlags_FramePadding;

            // Highlight when the model's representative node is selected
            if (selection.GetSelectedModel() == model &&
                selection.GetSelectedNodeIndex() == modelNodeIdx &&
                selection.GetSelectionType() == SelectionType::Node)
            {
                modelFlags |= ImGuiTreeNodeFlags_Selected;
            }

            if (m_modelToExpand == model)
            {
                ImGui::SetNextItemOpen(true);
            }

            bool modelOpen = ImGui::TreeNodeEx((void *)(intptr_t)id, modelFlags, "%s", displayName.c_str());
            if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::IsItemToggledOpen())
            {
                if (modelNodeIdx >= 0)
                {
                    selection.Select(model, modelNodeIdx, SelectionType::Node);
                    ImGui::SetWindowFocus("Properties");
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                Camera *camera = scene.GetActiveCamera();
                vec3 min = vec3(FLT_MAX);
                vec3 max = vec3(-FLT_MAX);
                for (const auto &node : model->GetNodeInfos())
                {
                    min = glm::min(min, node.worldBoundingBox.min);
                    max = glm::max(max, node.worldBoundingBox.max);
                }
                if (min.x != FLT_MAX)
                {
                    vec3 center = (min + max) * 0.5f;
                    float dist = glm::length(max - min);
                    vec3 dir = camera->GetFront();
                    camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                }
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Focus"))
                {
                    Camera *camera = scene.GetActiveCamera();
                    vec3 min = vec3(FLT_MAX);
                    vec3 max = vec3(-FLT_MAX);
                    int nodeToSelect = -1;
                    for (int i = 0; i < model->GetNodeCount(); i++)
                    {
                        const auto &node = model->GetNodeInfos()[i];
                        min = glm::min(min, node.worldBoundingBox.min);
                        max = glm::max(max, node.worldBoundingBox.max);
                        if (nodeToSelect < 0 && model->GetNodeMesh(i) >= 0)
                            nodeToSelect = i;
                    }

                    if (nodeToSelect >= 0)
                    {
                        selection.Select(model, nodeToSelect, SelectionType::Node);
                    }

                    if (min.x != FLT_MAX)
                    {
                        vec3 center = (min + max) * 0.5f;
                        float dist = glm::length(max - min);
                        vec3 dir = camera->GetFront();
                        camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                        ImGui::SetWindowFocus("Properties");
                    }
                }
                if (ImGui::MenuItem("Rename"))
                {
                    s_renameModel = model;
                    s_renameNode = -1;
                    snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", name.c_str());
                    s_openRenamePopup = true;
                }
                if (ImGui::MenuItem("Delete"))
                {
                    recordUndo();
                    modelsToDelete.push_back(model);
                }
                ImGui::EndPopup();
            }

            // Drag & drop target on model entry (acts as root node)
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
                {
                    HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                    if (data.modelId == model->GetId() && modelNodeIdx >= 0)
                    {
                        model->ReparentNode(data.nodeIndex, modelNodeIdx);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (modelOpen)
            {
                // Use pre-computed adjacency (nodeChildren, roots)
                auto &children = nodeChildren;

                // Helper to draw a mesh entry under a node
                auto DrawMeshEntry = [&](int nodeIndex, int meshIndex)
                {
                    const auto &node = nodes[nodeIndex];
                    uintptr_t meshUniqueId = (id << 16) ^ (nodeIndex + 0x10000);
                    std::string meshDisplayName = std::string(ICON_FA_SHAPES) + "  Mesh";

                    ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                   ImGuiTreeNodeFlags_Leaf |
                                                   ImGuiTreeNodeFlags_FramePadding;

                    if (selection.GetSelectedModel() == model && selection.GetSelectedNodeIndex() == nodeIndex && selection.GetSelectionType() == SelectionType::Mesh)
                        meshFlags |= ImGuiTreeNodeFlags_Selected;

                    bool meshOpen = ImGui::TreeNodeEx((void *)meshUniqueId, meshFlags, "%s", meshDisplayName.c_str());

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        selection.Select(model, nodeIndex, SelectionType::Mesh);
                        ImGui::SetWindowFocus("Properties");
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        Camera *camera = scene.GetActiveCamera();
                        const auto &meshInfos = model->GetMeshInfos();
                        if (meshIndex >= 0 && meshIndex < static_cast<int>(meshInfos.size()))
                        {
                            vec3 min = node.worldBoundingBox.min;
                            vec3 max = node.worldBoundingBox.max;
                            vec3 center = (min + max) * 0.5f;
                            float dist = glm::length(max - min);
                            vec3 dir = camera->GetFront();
                            camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                        }
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Focus"))
                        {
                            selection.Select(model, nodeIndex, SelectionType::Mesh);
                            Camera *camera = scene.GetActiveCamera();
                            const auto &meshInfos = model->GetMeshInfos();
                            if (meshIndex >= 0 && meshIndex < static_cast<int>(meshInfos.size()))
                            {
                                vec3 min = node.worldBoundingBox.min;
                                vec3 max = node.worldBoundingBox.max;
                                vec3 center = (min + max) * 0.5f;
                                float dist = glm::length(max - min);
                                vec3 dir = camera->GetFront();
                                camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                                ImGui::SetWindowFocus("Properties");
                            }
                        }
                        if (ImGui::MenuItem("Delete"))
                        {
                            recordUndo();
                            EventSystem::PushEvent(EventType::MeshRemoved,
                                                   std::make_pair(model, nodeIndex));
                        }
                        ImGui::EndPopup();
                    }

                    if (meshOpen)
                        ImGui::TreePop();
                };

                // Recursive draw nodes with icons
                auto DrawNode = [&](auto &&self, int nodeIndex) -> void
                {
                    const auto &node = nodes[nodeIndex];
                    bool hasChildren = !children[nodeIndex].empty();
                    int meshIndex = model->GetNodeMesh(nodeIndex);
                    bool hasMesh = meshIndex >= 0;

                    // Auto-expand parents
                    if (m_modelToExpand == model && m_nodesToExpand.find(nodeIndex) != m_nodesToExpand.end())
                    {
                        ImGui::SetNextItemOpen(true);
                    }

                    // Node is a leaf only if it has no children AND no mesh to show
                    bool isLeaf = !hasChildren && !hasMesh;

                    // Use a unique ID mixing model ID and node Index to avoid conflicts
                    uintptr_t uniqueId = (id << 16) ^ nodeIndex;

                    // Choose icon based on node type
                    const char *icon;
                    std::string nodeName = node.name;

                    // Simple heuristic for node type (can be extended)
                    std::string lowerName = nodeName;
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                    if (lowerName.find("camera") != std::string::npos || lowerName.find("cam") != std::string::npos)
                    {
                        icon = ICON_FA_VIDEO;
                        // Special handling for Camera selection
                        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                        {
                            selection.Select(model, nodeIndex, SelectionType::Camera);
                            ImGui::SetWindowFocus("Properties");
                        }
                    }
                    else if (lowerName.find("light") != std::string::npos || lowerName.find("lamp") != std::string::npos)
                    {
                        icon = ICON_FA_LIGHTBULB;
                    }
                    else
                    {
                        icon = ICON_FA_VECTOR_SQUARE; // Node icon
                    }

                    std::string displayNodeName = std::string(icon) + "  " + nodeName;

                    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                   ImGuiTreeNodeFlags_OpenOnArrow |
                                                   ImGuiTreeNodeFlags_FramePadding;
                    if (isLeaf)
                        nodeFlags |= ImGuiTreeNodeFlags_Leaf;

                    // Highlight if selected (when node itself is selected, not its mesh)
                    if (selection.GetSelectedModel() == model && selection.GetSelectedNodeIndex() == nodeIndex && selection.GetSelectionType() == SelectionType::Node)
                    {
                        nodeFlags |= ImGuiTreeNodeFlags_Selected;
                        if (m_scrollToSelection && m_modelToExpand == model)
                        {
                            // ImGui::SetScrollHereY(); // User requested to disable centering scroll
                            m_scrollToSelection = false;
                            m_modelToExpand = nullptr;
                            m_nodesToExpand.clear();
                        }
                    }

                    bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", displayNodeName.c_str());

                    // Drag & Drop Source
                    if (ImGui::BeginDragDropSource())
                    {
                        HierarchyDragDropPayload payload;
                        payload.modelId = model->GetId();
                        payload.nodeIndex = nodeIndex;
                        ImGui::SetDragDropPayload("HIERARCHY_NODE", &payload, sizeof(HierarchyDragDropPayload));
                        ImGui::Text("%s", nodeName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::IsItemToggledOpen())
                    {
                        selection.Select(model, nodeIndex, SelectionType::Node);
                        ImGui::SetWindowFocus("Properties");
                    }

                    // Drag & Drop Target
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
                        {
                            HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                            if (data.modelId == model->GetId())
                            {
                                PE_INFO("Hierarchy: Dropped node %d onto node %d", data.nodeIndex, nodeIndex);
                                model->ReparentNode(data.nodeIndex, nodeIndex);
                            }
                        }

                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                        {
                            const char *pathStr = (const char *)payload->Data;
                            std::filesystem::path path(pathStr);

                            // Check extension (simplified check)
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
                                        if (Model *m = Model::Load(path))
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
                        vec3 center = (node.worldBoundingBox.min + node.worldBoundingBox.max) * 0.5f;
                        float dist = glm::length(node.worldBoundingBox.max - node.worldBoundingBox.min);
                        vec3 dir = camera->GetFront();
                        camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Focus"))
                        {
                            selection.Select(model, nodeIndex, SelectionType::Node);
                            Camera *camera = scene.GetActiveCamera();
                            vec3 center = (node.worldBoundingBox.min + node.worldBoundingBox.max) * 0.5f;
                            float dist = glm::length(node.worldBoundingBox.max - node.worldBoundingBox.min);
                            vec3 dir = camera->GetFront();
                            camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                            ImGui::SetWindowFocus("Properties");
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            s_renameModel = model;
                            s_renameNode = nodeIndex;
                            snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", nodeName.c_str());
                            s_openRenamePopup = true;
                        }
                        if (ImGui::MenuItem("Delete"))
                        {
                            recordUndo();
                            EventSystem::PushEvent(EventType::NodeRemoved,
                                                   std::make_pair(model, nodeIndex));
                        }
                        ImGui::EndPopup();
                    }

                    if (nodeOpen)
                    {
                        if (hasMesh)
                            DrawMeshEntry(nodeIndex, meshIndex);

                        for (int childIndex : children[nodeIndex])
                            self(self, childIndex);

                        ImGui::TreePop();
                    }
                };

                // Root nodes are merged into the model entry - show their
                // meshes and children directly, skipping the root node entries
                for (int rootIndex : roots)
                {
                    int meshIndex = model->GetNodeMesh(rootIndex);
                    if (meshIndex >= 0)
                        DrawMeshEntry(rootIndex, meshIndex);

                    for (int childIndex : children[rootIndex])
                        DrawNode(DrawNode, childIndex);
                }

                ImGui::TreePop();
            }
        }

        // Fill remaining space with dummy to allow dropping in empty area
        ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.y < 50.0f)
            available.y = 50.0f; // Minimum drop area
        ImGui::Dummy(available);

        // Window-wide Drop Target for loading models from FileBrowser
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char *pathStr = (const char *)payload->Data;
                std::filesystem::path path(pathStr);

                // Check extension
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
                            if (Model *m = Model::Load(path))
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
                if (s_renameNode <= -100)
                {
                    // Emitter rename (sentinel: -(index + 100))
                    int emitterIdx = -(s_renameNode + 100);
                    ParticleManager *pm = scene.GetParticleManager();
                    if (pm)
                    {
                        auto &emNames = pm->GetEmitterNames();
                        if (emitterIdx >= 0 && emitterIdx < static_cast<int>(emNames.size()))
                            emNames[emitterIdx] = s_renameBuf;
                    }
                }
                else if (s_renameModel)
                {
                    std::string newName = s_renameBuf;
                    if (s_renameNode == -1)
                        s_renameModel->SetLabel(newName);
                    else
                    {
                        auto &infos = s_renameModel->GetNodeInfos();
                        if (s_renameNode >= 0 && s_renameNode < static_cast<int>(infos.size()))
                            infos[s_renameNode].name = newName;
                    }
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
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        for (auto model : modelsToDelete)
        {
            EventSystem::PushEvent(EventType::ModelRemoved, model);
        }

        ImGui::End();

        if (styleVarCount > 0)
            ImGui::PopStyleVar(styleVarCount);
        if (styleColorCount > 0)
            ImGui::PopStyleColor(styleColorCount);
    }
} // namespace pe
