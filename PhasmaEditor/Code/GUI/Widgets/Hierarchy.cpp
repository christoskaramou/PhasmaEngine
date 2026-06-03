#include "Hierarchy.h"
#include "Camera/Camera.h"
#include "FileSelector.h"
#include "GUI/GUI.h"
#include "GUI/GUIState.h"
#include "GUI/UndoRedo.h"
#include "GUI/IconsFontAwesome.h"
#include "Particles/ParticleManager.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"
#include "Scene/SelectionManager.h"
#include "Script/ScriptSystem.h"
#include "ScriptEditor.h"
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
        const ImVec4 HeaderHovered = ImVec4(0.17f, 0.36f, 0.53f, 1.0f);
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
        GUIState::s_hierarchyFocused = ImGui::IsWindowFocused();

        Scene &scene = *GetActiveScene();
        auto &selection = SelectionManager::Instance();

        auto recordSnapshot = [&scene](const char *label)
        { UndoRedo::Instance().RecordSnapshot(scene, label); };
        auto recordUndo = [&recordSnapshot]()
        { recordSnapshot("Deleted Node"); };

        auto createSkybox = [&scene, &selection, &recordSnapshot]()
        {
            if (NodeId *existing = scene.GetSkyboxNode())
            {
                selection.Select(existing, SelectionType::Node);
                return;
            }

            recordSnapshot("Added Skybox");
            NodeId *node = scene.CreateSkyboxNode();
            selection.Select(node, SelectionType::Node);
        };

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
                recordSnapshot("Added Camera");
                scene.AddCamera();
            }

            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional Light"))
                {
                    recordSnapshot("Added Directional Light");
                    scene.CreateDirectionalLight();
                }
                if (ImGui::MenuItem("Point Light"))
                {
                    recordSnapshot("Added Point Light");
                    scene.CreatePointLight();
                }
                if (ImGui::MenuItem("Spot Light"))
                {
                    recordSnapshot("Added Spot Light");
                    scene.CreateSpotLight();
                }
                if (ImGui::MenuItem("Area Light"))
                {
                    recordSnapshot("Added Area Light");
                    scene.CreateAreaLight();
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Empty Node"))
            {
                recordSnapshot("Added Node");
                NodeId *node = scene.CreateNode("Empty Node");
                selection.Select(node, SelectionType::Node);
            }

            if (!scene.GetSkyboxNode() && ImGui::MenuItem("Skybox"))
                createSkybox();

            if (ImGui::BeginMenu("Mesh"))
            {
                auto AddPrimitive = [&](ModelAsset *m)
                {
                    recordSnapshot("Added Mesh");
                    EventSystem::PushEvent(EventType::ModelLoaded, m);
                    // After model is added to scene, select its first root node
                    // The event handler will call AddModel which creates NodeIds
                };
                if (ImGui::MenuItem("Plane"))
                    AddPrimitive(Primitives::CreatePlane());
                if (ImGui::MenuItem("Grid"))
                    AddPrimitive(Primitives::CreateGrid());
                if (ImGui::MenuItem("Cube"))
                    AddPrimitive(Primitives::CreateCube());
                if (ImGui::MenuItem("Sphere"))
                    AddPrimitive(Primitives::CreateSphere());
                if (ImGui::MenuItem("UV Sphere"))
                    AddPrimitive(Primitives::CreateUvSphere());
                if (ImGui::MenuItem("Ico Sphere"))
                    AddPrimitive(Primitives::CreateIcoSphere());
                if (ImGui::MenuItem("Cylinder"))
                    AddPrimitive(Primitives::CreateCylinder());
                if (ImGui::MenuItem("Cone"))
                    AddPrimitive(Primitives::CreateCone());
                if (ImGui::MenuItem("Pyramid"))
                    AddPrimitive(Primitives::CreatePyramid());
                if (ImGui::MenuItem("Torus"))
                    AddPrimitive(Primitives::CreateTorus());
                if (ImGui::MenuItem("Circle"))
                    AddPrimitive(Primitives::CreateCircle());
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
        static int s_renameEmitterIndex = -1;
        static char s_renameBuf[128] = "";
        static bool s_openRenamePopup = false;
        std::vector<NodeId *> nodesToDelete;

        // Check for selection change (auto-expand)
        NodeId *currentSelectedNode = selection.GetSelectedNode();
        if (m_lastSelectedNode && !scene.IsNodeAlive(m_lastSelectedNode))
            m_lastSelectedNode = nullptr;
        if (m_nodeToExpand && !scene.IsNodeAlive(m_nodeToExpand))
        {
            m_nodeToExpand = nullptr;
            m_nodesToExpand.clear();
            m_scrollToSelection = false;
        }
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

        // Root Node - all scene objects live under here
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_FramePadding |
                                       ImGuiTreeNodeFlags_OpenOnArrow;
        bool rootOpen = ImGui::TreeNodeEx("##RootNode", rootFlags, "%s  Root Node", ICON_FA_CUBE);

        if (ImGui::BeginPopupContextItem("RootNodeContext"))
        {
            if (ImGui::MenuItem("Camera"))
            {
                recordSnapshot("Added Camera");
                scene.AddCamera();
            }
            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional Light"))
                {
                    recordSnapshot("Added Directional Light");
                    scene.CreateDirectionalLight();
                }
                if (ImGui::MenuItem("Point Light"))
                {
                    recordSnapshot("Added Point Light");
                    scene.CreatePointLight();
                }
                if (ImGui::MenuItem("Spot Light"))
                {
                    recordSnapshot("Added Spot Light");
                    scene.CreateSpotLight();
                }
                if (ImGui::MenuItem("Area Light"))
                {
                    recordSnapshot("Added Area Light");
                    scene.CreateAreaLight();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Empty Node"))
            {
                recordSnapshot("Added Node");
                NodeId *node = scene.CreateNode("Empty Node");
                selection.Select(node, SelectionType::Node);
            }
            if (!scene.GetSkyboxNode() && ImGui::MenuItem("Skybox"))
                createSkybox();
            if (ImGui::BeginMenu("Mesh"))
            {
                auto AddPrim = [&](ModelAsset *m)
                {
                    recordSnapshot("Added Mesh");
                    EventSystem::PushEvent(EventType::ModelLoaded, m);
                };
                if (ImGui::MenuItem("Plane"))
                    AddPrim(Primitives::CreatePlane());
                if (ImGui::MenuItem("Grid"))
                    AddPrim(Primitives::CreateGrid());
                if (ImGui::MenuItem("Cube"))
                    AddPrim(Primitives::CreateCube());
                if (ImGui::MenuItem("Sphere"))
                    AddPrim(Primitives::CreateSphere());
                if (ImGui::MenuItem("UV Sphere"))
                    AddPrim(Primitives::CreateUvSphere());
                if (ImGui::MenuItem("Ico Sphere"))
                    AddPrim(Primitives::CreateIcoSphere());
                if (ImGui::MenuItem("Cylinder"))
                    AddPrim(Primitives::CreateCylinder());
                if (ImGui::MenuItem("Cone"))
                    AddPrim(Primitives::CreateCone());
                if (ImGui::MenuItem("Pyramid"))
                    AddPrim(Primitives::CreatePyramid());
                if (ImGui::MenuItem("Torus"))
                    AddPrim(Primitives::CreateTorus());
                if (ImGui::MenuItem("Circle"))
                    AddPrim(Primitives::CreateCircle());
                if (ImGui::MenuItem("Quad"))
                    AddPrim(Primitives::CreateQuad());
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        if (rootOpen)
        {

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
            // Recursive draw node
            auto DrawNode = [&](auto &&self, NodeId *node) -> void
            {
                const std::string &nodeName = scene.GetNodeName(node);
                auto children = scene.GetChildren(node);
                bool hasChildren = !children.empty();

                // Auto-expand parents
                if (m_nodesToExpand.find(node) != m_nodesToExpand.end())
                {
                    ImGui::SetNextItemOpen(true);
                }

                bool isLeaf = !hasChildren;

                uintptr_t uniqueId = reinterpret_cast<uintptr_t>(node);

                // Choose icon based on component flags
                uint32_t nodeCompFlags = scene.GetComponentFlags(node);
                const char *icon;
                if (nodeCompFlags & Component_Camera)
                    icon = ICON_FA_VIDEO;
                else if (nodeCompFlags & Component_Light)
                    icon = ICON_FA_LIGHTBULB;
                else if (nodeCompFlags & Component_Skybox)
                    icon = ICON_FA_SUN;
                else
                    icon = ICON_FA_VECTOR_SQUARE;

                std::string displayName = nodeName;
                if (nodeCompFlags & Component_Camera)
                {
                    Camera *thisCam = scene.GetCameraForNode(node);
                    if (thisCam && thisCam == scene.GetActiveCamera())
                        displayName += " (Main)";
                }

                // Look up script error for this node
                std::string scriptError;
                if (nodeCompFlags & Component_Script)
                {
                    if (auto *ss = GetGlobalSystem<ScriptSystem>())
                        if (auto *inst = ss->FindNodeInstance(node))
                            scriptError = inst->lastError;
                }

                std::string displayNodeName = std::string(icon) + "  " + displayName;
                if (!scriptError.empty())
                    displayNodeName += "  " ICON_FA_TRIANGLE_EXCLAMATION;
                const bool nodeEnabled = scene.IsNodeEnabled(node);
                const bool hierarchyEnabled = scene.IsNodeHierarchyEnabled(node);

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

                ImGui::PushID(node);
                bool enabledEdit = nodeEnabled;
                if (ImGui::Checkbox("##Enabled", &enabledEdit))
                {
                    recordSnapshot(enabledEdit ? "Enabled Node" : "Disabled Node");
                    scene.SetNodeEnabled(node, enabledEdit);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", enabledEdit ? "Disable node and subtree rendering" : "Enable node rendering");
                ImGui::SameLine(0.0f, 4.0f);

                if (!hierarchyEnabled)
                    ImGui::PushStyleColor(ImGuiCol_Text, HierarchyStyle::TextDisabled);
                bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", displayNodeName.c_str());
                if (!hierarchyEnabled)
                    ImGui::PopStyleColor();
                ImGui::PopID();

                if (!scriptError.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Script error:\n%s", scriptError.c_str());

                // Drag & Drop Source — SourceAllowNullID lets the drag initiate
                // from the tree node label even when OpenOnArrow is set
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    HierarchyDragDropPayload payload;
                    payload.node = node;
                    ImGui::SetDragDropPayload("HIERARCHY_NODE", &payload, sizeof(HierarchyDragDropPayload));
                    ImGui::Text("%s", nodeName.c_str());
                    ImGui::EndDragDropSource();
                }

                if ((ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::IsItemToggledOpen())
                    selection.Select(node, SelectionType::Node);

                // Focus Properties on release only — calling SetWindowFocus on press clears
                // g.ActiveId via FocusWindow→ClearActiveID, which breaks BeginDragDropSource.
                if (ImGui::IsItemHovered() &&
                    ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                    !ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                    !ImGui::IsItemToggledOpen())
                    ImGui::SetWindowFocus("Properties");

                // Drag & Drop Target
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE"))
                    {
                        HierarchyDragDropPayload data = *(const HierarchyDragDropPayload *)payload->Data;
                        if (scene.IsNodeAlive(data.node) && data.node != node && scene.GetParent(data.node) != node)
                        {
                            recordSnapshot("Reparented Node");
                            scene.ReparentNode(data.node, node);
                        }
                    }

                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    {
                        const char *pathStr = (const char *)payload->Data;
                        std::filesystem::path path(pathStr);

                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        // Only cooked .pemesh loads into the scene; source models are import-only (File > Import).
                        bool isModel = (ext == ".pemesh");

                        if (isModel && !GUIState::s_modelLoading)
                        {
                            auto loadTask = [path, node]()
                            {
                                GUIState::s_modelLoading = true;
                                try
                                {
                                    if (ModelAsset *m = ModelAsset::Load(path))
                                        EventSystem::PushEvent(EventType::ModelLoadedForNode,
                                                               Scene::ModelLoadForNodeRequest{node, m});
                                }
                                catch (const std::exception &e)
                                {
                                    PE_WARN("[Scene] Failed to load model: %s", e.what());
                                }
                                GUIState::s_modelLoading = false;
                            };
                            ThreadPool::GUI.Enqueue(loadTask);
                        }

                        if (ext == ".lua")
                            scene.SetNodeScript(node, path.string());
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!(nodeCompFlags & Component_Skybox) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
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
                    if (!(nodeCompFlags & Component_Skybox) && ImGui::MenuItem("Focus"))
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
                    if (nodeCompFlags & Component_Camera)
                    {
                        Camera *thisCam = scene.GetCameraForNode(node);
                        if (thisCam && thisCam != scene.GetActiveCamera() && ImGui::MenuItem("Set as Active Camera"))
                            scene.SetActiveCamera(thisCam);
                    }
                    if (ImGui::MenuItem("Rename"))
                    {
                        s_renameNode = node;
                        s_renameEmitterIndex = -1;
                        snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", nodeName.c_str());
                        s_openRenamePopup = true;
                    }
                    if (ImGui::BeginMenu("Add"))
                    {
                        if (ImGui::MenuItem("Camera"))
                        {
                            recordSnapshot("Added Camera");
                            scene.AddCamera(node);
                        }

                        if (ImGui::BeginMenu("Light"))
                        {
                            if (ImGui::MenuItem("Directional Light"))
                            {
                                recordSnapshot("Added Directional Light");
                                scene.CreateDirectionalLight(node);
                            }
                            if (ImGui::MenuItem("Point Light"))
                            {
                                recordSnapshot("Added Point Light");
                                scene.CreatePointLight(node);
                            }
                            if (ImGui::MenuItem("Spot Light"))
                            {
                                recordSnapshot("Added Spot Light");
                                scene.CreateSpotLight(node);
                            }
                            if (ImGui::MenuItem("Area Light"))
                            {
                                recordSnapshot("Added Area Light");
                                scene.CreateAreaLight(node);
                            }
                            ImGui::EndMenu();
                        }

                        if (ImGui::MenuItem("Empty Node"))
                        {
                            recordSnapshot("Added Node");
                            NodeId *newNode = scene.CreateNode("Empty Node", node);
                            scene.MarkNodeDirty(newNode);
                            selection.Select(newNode, SelectionType::Node);
                        }

                        uint32_t componentFlags = scene.GetComponentFlags(node);

                        if (!(componentFlags & Component_Mesh) && ImGui::BeginMenu("Mesh"))
                        {
                            auto AttachPrimitive = [&](ModelAsset *m)
                            {
                                recordSnapshot("Added Mesh Component");
                                EventSystem::PushEvent(EventType::PrimitiveAttachedToNode,
                                                       Scene::PrimitiveAttachRequest{node, m});
                            };
                            if (ImGui::MenuItem("Plane"))
                                AttachPrimitive(Primitives::CreatePlane());
                            if (ImGui::MenuItem("Grid"))
                                AttachPrimitive(Primitives::CreateGrid());
                            if (ImGui::MenuItem("Cube"))
                                AttachPrimitive(Primitives::CreateCube());
                            if (ImGui::MenuItem("Sphere"))
                                AttachPrimitive(Primitives::CreateSphere());
                            if (ImGui::MenuItem("UV Sphere"))
                                AttachPrimitive(Primitives::CreateUvSphere());
                            if (ImGui::MenuItem("Ico Sphere"))
                                AttachPrimitive(Primitives::CreateIcoSphere());
                            if (ImGui::MenuItem("Cylinder"))
                                AttachPrimitive(Primitives::CreateCylinder());
                            if (ImGui::MenuItem("Cone"))
                                AttachPrimitive(Primitives::CreateCone());
                            if (ImGui::MenuItem("Pyramid"))
                                AttachPrimitive(Primitives::CreatePyramid());
                            if (ImGui::MenuItem("Torus"))
                                AttachPrimitive(Primitives::CreateTorus());
                            if (ImGui::MenuItem("Circle"))
                                AttachPrimitive(Primitives::CreateCircle());
                            if (ImGui::MenuItem("Quad"))
                                AttachPrimitive(Primitives::CreateQuad());
                            ImGui::EndMenu();
                        }

                        if (!(componentFlags & Component_Script) && ImGui::BeginMenu("Lua Script"))
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

                        if (ImGui::MenuItem("Particle Emitter"))
                        {
                            ParticleManager *pm = scene.GetParticleManager();
                            if (pm)
                            {
                                recordSnapshot("Added Particle Emitter");
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
                        bool canDelete = true;
                        if (nodeCompFlags & Component_Camera)
                        {
                            Camera *cam = scene.GetCameraForNode(node);
                            canDelete = cam && scene.GetCameras().size() > 1;
                        }
                        if (canDelete)
                        {
                            recordUndo();
                            nodesToDelete.push_back(node);
                        }
                    }
                    ImGui::EndPopup();
                }

                if (nodeOpen)
                {
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

                    // Only cooked .pemesh loads into the scene; source models are import-only (File > Import).
                    bool isModel = (ext == ".pemesh");

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
                    if (scene.IsNodeAlive(data.node) && scene.GetParent(data.node))
                    {
                        recordSnapshot("Reparented Node");
                        scene.ReparentNode(data.node, nullptr);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TreePop(); // Root Node
        } // if (rootOpen)

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
                    if (scene.IsNodeAlive(s_renameNode))
                    {
                        scene.SetNodeName(s_renameNode, s_renameBuf);
                        // Sync camera/light names stored in their respective systems
                        Camera *cam = scene.GetCameraForNode(s_renameNode);
                        if (cam)
                            cam->SetName(s_renameBuf);
                        else
                        {
                            auto [lt, idx] = scene.GetLightForNode(s_renameNode);
                            if (idx >= 0)
                            {
                                if (lt == LightType::Directional)
                                    scene.GetDirectionalLights()[idx].name = s_renameBuf;
                                else if (lt == LightType::Point)
                                    scene.GetPointLights()[idx].name = s_renameBuf;
                                else if (lt == LightType::Spot)
                                    scene.GetSpotLights()[idx].name = s_renameBuf;
                                else if (lt == LightType::Area)
                                    scene.GetAreaLights()[idx].name = s_renameBuf;
                            }
                        }
                    }
                }
                s_renameNode = nullptr;
                s_renameEmitterIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                s_renameNode = nullptr;
                s_renameEmitterIndex = -1;
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
                {
                    recordSnapshot("Added Camera");
                    scene.AddCamera();
                }

                if (ImGui::BeginMenu("Light"))
                {
                    if (ImGui::MenuItem("Directional Light"))
                    {
                        recordSnapshot("Added Directional Light");
                        scene.CreateDirectionalLight();
                    }
                    if (ImGui::MenuItem("Point Light"))
                    {
                        recordSnapshot("Added Point Light");
                        scene.CreatePointLight();
                    }
                    if (ImGui::MenuItem("Spot Light"))
                    {
                        recordSnapshot("Added Spot Light");
                        scene.CreateSpotLight();
                    }
                    if (ImGui::MenuItem("Area Light"))
                    {
                        recordSnapshot("Added Area Light");
                        scene.CreateAreaLight();
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Empty Node"))
                {
                    recordSnapshot("Added Node");
                    NodeId *node = scene.CreateNode("Empty Node");
                    selection.Select(node, SelectionType::Node);
                }

                if (!scene.GetSkyboxNode() && ImGui::MenuItem("Skybox"))
                    createSkybox();

                if (ImGui::BeginMenu("Mesh"))
                {
                    auto AddPrim = [&](ModelAsset *m)
                    {
                        recordSnapshot("Added Mesh");
                        EventSystem::PushEvent(EventType::ModelLoaded, m);
                    };
                    if (ImGui::MenuItem("Plane"))
                        AddPrim(Primitives::CreatePlane());
                    if (ImGui::MenuItem("Grid"))
                        AddPrim(Primitives::CreateGrid());
                    if (ImGui::MenuItem("Cube"))
                        AddPrim(Primitives::CreateCube());
                    if (ImGui::MenuItem("Sphere"))
                        AddPrim(Primitives::CreateSphere());
                    if (ImGui::MenuItem("UV Sphere"))
                        AddPrim(Primitives::CreateUvSphere());
                    if (ImGui::MenuItem("Ico Sphere"))
                        AddPrim(Primitives::CreateIcoSphere());
                    if (ImGui::MenuItem("Cylinder"))
                        AddPrim(Primitives::CreateCylinder());
                    if (ImGui::MenuItem("Cone"))
                        AddPrim(Primitives::CreateCone());
                    if (ImGui::MenuItem("Pyramid"))
                        AddPrim(Primitives::CreatePyramid());
                    if (ImGui::MenuItem("Torus"))
                        AddPrim(Primitives::CreateTorus());
                    if (ImGui::MenuItem("Circle"))
                        AddPrim(Primitives::CreateCircle());
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
