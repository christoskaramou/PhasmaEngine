#include "Hierarchy.h"
#include "Camera/Camera.h"
#include "GUI/GUIState.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Scene/SelectionManager.h"
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

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Scene &scene = renderer->GetScene();
        auto &models = scene.GetModels();
        auto &selection = SelectionManager::Instance();

        static Model *s_renameModel = nullptr;
        static int s_renameNode = -1;
        static char s_renameBuf[128] = "";
        static bool s_openRenamePopup = false;
        std::vector<Model *> modelsToDelete;

        // Models listing
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
                        nodeFlags |= ImGuiTreeNodeFlags_Selected;

                    bool nodeOpen = ImGui::TreeNodeEx((void *)uniqueId, nodeFlags, "%s", displayNodeName.c_str());

                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    {
                        selection.Select(model, nodeIndex, SelectionType::Node);
                        ImGui::SetWindowFocus("Properties");
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
            scene.RemoveModel(model);
        }

        ImGui::End();

        if (styleVarCount > 0)
            ImGui::PopStyleVar(styleVarCount);
        if (styleColorCount > 0)
            ImGui::PopStyleColor(styleColorCount);
    }
} // namespace pe
