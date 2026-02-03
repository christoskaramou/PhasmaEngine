#include "Hierarchy.h"
#include "Camera/Camera.h"
#include "GUI/GUIState.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Model.h"
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

        static Model *s_renameModel = nullptr;
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

        // Models listing
        // Main Camera
        ImGuiTreeNodeFlags cameraFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                         ImGuiTreeNodeFlags_Leaf |
                                         ImGuiTreeNodeFlags_FramePadding;

        if (selection.GetSelectionType() == SelectionType::Camera)
            cameraFlags |= ImGuiTreeNodeFlags_Selected;

        bool cameraOpen = ImGui::TreeNodeEx((void *)"MainCamera", cameraFlags, "%s  Main Camera", ICON_FA_VIDEO);
        if (ImGui::IsItemClicked())
        {
            selection.Select(nullptr, -1, SelectionType::Camera);
            ImGui::SetWindowFocus("Properties");
        }
        if (cameraOpen)
        {
            ImGui::TreePop();
        }

        // Lights
        if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding))
        {
            ImGuiTreeNodeFlags lightFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_FramePadding;

            // Directional Light (Sun)
            if (selection.GetSelectionType() == SelectionType::Light &&
                selection.GetSelectedLightType() == LightType::Directional)
                lightFlags |= ImGuiTreeNodeFlags_Selected;
            else
                lightFlags &= ~ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx((void *)"DirectionalLight", lightFlags, "%s  Directional Light", ICON_FA_SUN);
            if (ImGui::IsItemClicked())
            {
                selection.Select(LightType::Directional, 0);
                ImGui::SetWindowFocus("Properties");
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                Camera *camera = scene.GetActiveCamera();
                camera->SetPosition(vec3(0, 0, 0) - camera->GetFront() * 10.0f);
            }
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Focus"))
                {
                    Camera *camera = scene.GetActiveCamera();
                    // Focus on origin for directional light
                    camera->SetPosition(vec3(0, 0, 0) - camera->GetFront() * 10.0f);
                }
                ImGui::EndPopup();
            }
            ImGui::TreePop();

            LightSystem *lightSystem = GetGlobalSystem<LightSystem>();

            // Point Lights
            for (int i = 0; i < (int)lightSystem->GetPointLights().size(); i++)
            {
                if (selection.GetSelectionType() == SelectionType::Light &&
                    selection.GetSelectedLightType() == LightType::Point &&
                    selection.GetSelectedLightIndex() == i)
                    lightFlags |= ImGuiTreeNodeFlags_Selected;
                else
                    lightFlags &= ~ImGuiTreeNodeFlags_Selected;

                std::string name = "Point Light " + std::to_string(i);
                ImGui::TreeNodeEx((void *)(intptr_t)(i + 1000), lightFlags, "%s  %s", ICON_FA_LIGHTBULB, name.c_str());
                if (ImGui::IsItemClicked())
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
                        Camera *camera = scene.GetActiveCamera();
                        vec3 pos = vec3(lightSystem->GetPointLights()[i].position);
                        camera->SetPosition(pos - camera->GetFront() * 5.0f);
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        // Reset light
                        lightSystem->GetPointLights()[i].color = vec4(0.0f);                          // Intensity 0
                        lightSystem->GetPointLights()[i].position = vec4(0.0f, 10000.0f, 0.0f, 0.0f); // Move away
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

                std::string name = "Spot Light " + std::to_string(i);
                ImGui::TreeNodeEx((void *)(intptr_t)(i + 2000), lightFlags, "%s  %s", ICON_FA_LIGHTBULB, name.c_str());
                if (ImGui::IsItemClicked())
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
                        Camera *camera = scene.GetActiveCamera();
                        vec3 pos = lightSystem->GetSpotLights()[i].position;
                        camera->SetPosition(pos - camera->GetFront() * 5.0f);
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        // Reset light
                        lightSystem->GetSpotLights()[i].color = vec4(0.0f);                          // Intensity 0
                        lightSystem->GetSpotLights()[i].position = vec4(0.0f, 10000.0f, 0.0f, 0.0f); // Move away
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

                std::string name = "Area Light " + std::to_string(i);
                ImGui::TreeNodeEx((void *)(intptr_t)(i + 3000), lightFlags, "%s  %s", ICON_FA_LIGHTBULB, name.c_str());
                if (ImGui::IsItemClicked())
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
                        Camera *camera = scene.GetActiveCamera();
                        vec3 pos = lightSystem->GetAreaLights()[i].position;
                        camera->SetPosition(pos - camera->GetFront() * 5.0f);
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        // Reset light
                        lightSystem->GetAreaLights()[i].color = vec4(0.0f);                          // Intensity 0
                        lightSystem->GetAreaLights()[i].position = vec4(0.0f, 10000.0f, 0.0f, 0.0f); // Move away
                    }
                    ImGui::EndPopup();
                }
                ImGui::TreePop();
            }

            ImGui::TreePop();
        }

        for (auto model : models)
        {
            if (!model)
                continue;

            size_t id = model->GetId();
            std::string name = model->GetLabel();
            if (name.empty())
                name = "Model_" + std::to_string(id);

            // Add model icon
            std::string displayName = std::string(ICON_FA_CUBE) + "  " + name;

            ImGuiTreeNodeFlags modelFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_OpenOnArrow |
                                            ImGuiTreeNodeFlags_DefaultOpen |
                                            ImGuiTreeNodeFlags_FramePadding;

            if (selection.GetSelectedModel() == model && selection.GetSelectedNodeIndex() < 0)
                modelFlags |= ImGuiTreeNodeFlags_Selected;

            if (m_modelToExpand == model)
            {
                ImGui::SetNextItemOpen(true);
            }

            bool modelOpen = ImGui::TreeNodeEx((void *)(intptr_t)id, modelFlags, "%s", displayName.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                int nodeToSelect = -1;
                for (int i = 0; i < model->GetNodeCount(); i++)
                {
                    if (model->GetNodeMesh(i) >= 0)
                    {
                        nodeToSelect = i;
                        break;
                    }
                }
                if (nodeToSelect >= 0)
                {
                    selection.Select(model, nodeToSelect, SelectionType::Node);
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
                if (ImGui::MenuItem("Rename"))
                {
                    s_renameModel = model;
                    s_renameNode = -1;
                    strcpy(s_renameBuf, name.c_str());
                    s_openRenamePopup = true;
                }
                if (ImGui::MenuItem("Delete"))
                {
                    modelsToDelete.push_back(model);
                }
                ImGui::EndPopup();
            }

            if (modelOpen)
            {
                const auto &nodes = model->GetNodeInfos();
                int nodeCount = static_cast<int>(nodes.size());

                // Build adjacency list for efficient traversal
                std::vector<std::vector<int>> children(nodeCount);
                std::vector<int> roots;
                for (int i = 0; i < nodeCount; ++i)
                {
                    if (nodes[i].parent >= 0 && nodes[i].parent < nodeCount)
                        children[nodes[i].parent].push_back(i);
                    else
                        roots.push_back(i);
                }

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

                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    {
                        selection.Select(model, nodeIndex, SelectionType::Node);
                        // ImGui::SetWindowFocus("Properties"); // Removed to prevent interrupting drag
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
                                    Model::Load(path);
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
                            Camera *camera = scene.GetActiveCamera();
                            vec3 center = (node.worldBoundingBox.min + node.worldBoundingBox.max) * 0.5f;
                            float dist = glm::length(node.worldBoundingBox.max - node.worldBoundingBox.min);
                            vec3 dir = camera->GetFront();
                            camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            s_renameModel = model;
                            s_renameNode = nodeIndex;
                            strcpy(s_renameBuf, nodeName.c_str());
                            s_openRenamePopup = true;
                        }
                        ImGui::EndPopup();
                    }

                    if (nodeOpen)
                    {
                        // If this node has a mesh, show it as a child
                        if (hasMesh)
                        {
                            uintptr_t meshUniqueId = (id << 16) ^ (nodeIndex + 0x10000); // Different ID for mesh
                            std::string meshDisplayName = std::string(ICON_FA_SHAPES) + "  Mesh";

                            ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                           ImGuiTreeNodeFlags_Leaf |
                                                           ImGuiTreeNodeFlags_FramePadding;

                            // Mesh is selected if node is selected
                            if (selection.GetSelectedModel() == model && selection.GetSelectedNodeIndex() == nodeIndex && selection.GetSelectionType() == SelectionType::Mesh)
                                meshFlags |= ImGuiTreeNodeFlags_Selected;

                            bool meshOpen = ImGui::TreeNodeEx((void *)meshUniqueId, meshFlags, "%s", meshDisplayName.c_str());

                            if (ImGui::IsItemClicked())
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
                                    const auto &mesh = meshInfos[meshIndex];
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
                                    Camera *camera = scene.GetActiveCamera();
                                    const auto &meshInfos = model->GetMeshInfos();
                                    if (meshIndex >= 0 && meshIndex < static_cast<int>(meshInfos.size()))
                                    {
                                        const auto &mesh = meshInfos[meshIndex];
                                        vec3 min = node.worldBoundingBox.min;
                                        vec3 max = node.worldBoundingBox.max;
                                        vec3 center = (min + max) * 0.5f;
                                        float dist = glm::length(max - min);
                                        vec3 dir = camera->GetFront();
                                        camera->SetPosition(center - dir * glm::max(dist, camera->GetNearPlane()));
                                    }
                                }
                                ImGui::EndPopup();
                            }

                            if (meshOpen)
                                ImGui::TreePop();
                        }

                        // Draw child nodes
                        for (int childIndex : children[nodeIndex])
                        {
                            self(self, childIndex);
                        }
                        ImGui::TreePop();
                    }
                };

                for (int rootIndex : roots)
                {
                    DrawNode(DrawNode, rootIndex);
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
                        Model::Load(path);
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
                if (s_renameModel)
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
